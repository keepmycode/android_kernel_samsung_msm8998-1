/* drivers/net/pppopns.c
 *
 * Driver for PPP on PPTP Network Server / PPPoPNS Socket (RFC 2637)
 *
 * Copyright (C) 2009 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* This driver handles PPTP data packets between a RAW socket and a PPP channel.
 * The socket is created in the kernel space and connected to the same address
 * of the control socket. Outgoing packets are always sent with sequences but
 * without acknowledgements. Incoming packets with sequences are reordered
 * within a sliding window of one second. Currently reordering only happens when
 * a packet is received. It is done for simplicity since no additional locks or
 * threads are required. This driver should work on both IPv4 and IPv6. */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/workqueue.h>
#include <linux/skbuff.h>
#include <linux/file.h>
#include <linux/netdevice.h>
#include <linux/net.h>
#include <linux/ppp_defs.h>
#include <linux/if.h>
#include <linux/if_ppp.h>
#include <linux/if_pppox.h>
#include <linux/ppp_channel.h>
#include <asm/uaccess.h>

#define GRE_HEADER_SIZE		8
#define SC_GRE_SEQ_CHK  0x00008000  
#define PPTP_GRE_BITS		htons(0x2001)
#define PPTP_GRE_BITS_MASK	htons(0xEF7F)
#define PPTP_GRE_SEQ_BIT	htons(0x1000)
#define PPTP_GRE_ACK_BIT	htons(0x0080)
#define PPTP_GRE_TYPE		htons(0x880B)

#define PPP_ADDR	0xFF
#define PPP_CTRL	0x03

struct header {
	__u16	bits;
	__u16	type;
	__u16	length;
	__u16	call;
	__u32	sequence;
} __attribute__((packed));

struct meta {
	__u32 sequence;
	__u32 timestamp;
};

static inline struct meta *skb_meta(struct sk_buff *skb)
{
	return (struct meta *)skb->cb;
}

static void recv_queue_timer_callback(unsigned long data);
static void traverse_receive_queue(struct sock *sk)
{
	struct pppox_sock *po = pppox_sk(sk);
	struct pppopns_opt *opt = &pppox_sk(sk)->proto.pns;

	struct sk_buff *skb;
	struct sk_buff *skb1;
	struct meta *meta;
	__u32 now = jiffies;	

	/* Remove packets from receive queue as long as
	 * 1. the receive buffer is full,
	 * 2. they are queued longer than one second, or
	 * 3. there are no missing packets before them. */
	skb_queue_walk_safe(&sk->sk_receive_queue, skb, skb1) {
		meta = skb_meta(skb);
		if (atomic_read(&sk->sk_rmem_alloc) < sk->sk_rcvbuf &&
		    now - meta->timestamp < (HZ - 5) &&
		    meta->sequence != opt->recv_sequence)
		  break;
		skb_unlink(skb, &sk->sk_receive_queue);
		opt->recv_sequence = meta->sequence + 1;
		skb_orphan(skb);
		ppp_input(&po->chan, skb);
	}
	
	if (skb_queue_len(&sk->sk_receive_queue) > 0) {
		/* Start the timer. The timer will
		   expire after one second. When the 
		   timer expires, the receive_queue is 
		   checked and all packets older than 
		   one second are removed from the queue and
		   passed forward. */
	  	if (timer_pending(&po->recv_queue_timer)) {
	        	/* Something is wrong. Recv timer is already active. However, Ignoring...*/
		} else {	  
			init_timer(&po->recv_queue_timer);
			po->recv_queue_timer.data = (unsigned long)sk;
			po->recv_queue_timer.function = recv_queue_timer_callback;
			po->recv_queue_timer.expires = now + HZ;
			add_timer(&po->recv_queue_timer);
		}
	}
}

static void recv_queue_timer_callback(unsigned long data)
{
  struct sock *sk = (struct sock *)data;
  unsigned long flags;

  spin_lock_irqsave(&pppox_sk(sk)->recv_queue_lock, flags);
  traverse_receive_queue(sk);
  spin_unlock_irqrestore(&pppox_sk(sk)->recv_queue_lock, flags);
}

/******************************************************************************/

static int pppopns_recv_core(struct sock *sk_raw, struct sk_buff *skb)
{
	struct sock *sk = (struct sock *)sk_raw->sk_user_data;
	struct pppopns_opt *opt = &pppox_sk(sk)->proto.pns;
	struct meta *meta = skb_meta(skb);
	__u32 now = jiffies;
	struct header *hdr;
    unsigned long flags;

	/* Skip transport header */
	skb_pull(skb, skb_transport_header(skb) - skb->data);

	/* Drop the packet if GRE header is missing. */
	if (skb->len < GRE_HEADER_SIZE)
		goto drop;
	hdr = (struct header *)skb->data;

	/* Check the header. */
	if (hdr->type != PPTP_GRE_TYPE || hdr->call != opt->local ||
			(hdr->bits & PPTP_GRE_BITS_MASK) != PPTP_GRE_BITS)
		goto drop;

	/* Skip all fields including optional ones. */
	if (!skb_pull(skb, GRE_HEADER_SIZE +
			(hdr->bits & PPTP_GRE_SEQ_BIT ? 4 : 0) +
			(hdr->bits & PPTP_GRE_ACK_BIT ? 4 : 0)))
		goto drop;

	/* Check the length. */
	if (skb->len != ntohs(hdr->length))
		goto drop;

	/* Check the sequence if it is present. */
	if (hdr->bits & PPTP_GRE_SEQ_BIT) {
		meta->sequence = ntohl(hdr->sequence);
		if ((__s32)(meta->sequence - opt->recv_sequence) < 0)
			goto drop;
	}

	/* Skip PPP address and control if they are present. */
	if (skb->len >= 2 && skb->data[0] == PPP_ADDR &&
			skb->data[1] == PPP_CTRL)
		skb_pull(skb, 2);

	/* Fix PPP protocol if it is compressed. */
	if (skb->len >= 1 && skb->data[0] & 1)
		skb_push(skb, 1)[0] = 0;

	/* Drop the packet if PPP protocol is missing. */
	if (skb->len < 2)
		goto drop;

	/* Perform reordering if sequencing is enabled. */
	if (hdr->bits & PPTP_GRE_SEQ_BIT) {
		struct sk_buff *skb1;

		if (timer_pending(&pppox_sk(sk)->recv_queue_timer)) {
			del_timer_sync(&pppox_sk(sk)->recv_queue_timer);
		}

		spin_lock_irqsave(&pppox_sk(sk)->recv_queue_lock, flags);

		/* Insert the packet into receive queue in order. */
		skb_set_owner_r(skb, sk);
		skb_queue_walk(&sk->sk_receive_queue, skb1) {
			struct meta *meta1 = skb_meta(skb1);
			__s32 order = meta->sequence - meta1->sequence;
			if (order == 0) {
				spin_unlock_irqrestore(&pppox_sk(sk)->recv_queue_lock, flags);
				goto drop;
			}
			if (order < 0) {
				meta->timestamp = meta1->timestamp;
				skb_insert(skb1, skb, &sk->sk_receive_queue);
				skb = NULL;
				break;
			}
		}
		if (skb) {
			meta->timestamp = now;
			skb_queue_tail(&sk->sk_receive_queue, skb);
		}
		
		traverse_receive_queue(sk);
		spin_unlock_irqrestore(&pppox_sk(sk)->recv_queue_lock, flags);
		return NET_RX_SUCCESS;
	}

	/* Flush receive queue if sequencing is disabled. */
	skb_queue_purge(&sk->sk_receive_queue);
	skb_orphan(skb);
	ppp_input(&pppox_sk(sk)->chan, skb);
	return NET_RX_SUCCESS;
drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static void pppopns_recv(struct sock *sk_raw)
{
	struct sk_buff *skb;
	while ((skb = skb_dequeue(&sk_raw->sk_receive_queue))) {
		sock_hold(sk_raw);
		sk_receive_skb(sk_raw, skb, 0);
	}
}

static struct sk_buff_head delivery_queue;

static void pppopns_xmit_core(struct work_struct *delivery_work)
{
	mm_segment_t old_fs = get_fs();
	struct sk_buff *skb;

	set_fs(KERNEL_DS);
	while ((skb = skb_dequeue(&delivery_queue))) {
		struct sock *sk_raw = skb->sk;
		struct kvec iov = {.iov_base = skb->data, .iov_len = skb->len};
		struct msghdr msg = { 0 };

		iov_iter_kvec(&msg.msg_iter, WRITE | ITER_KVEC, &iov, 1,
			      skb->len);
		sk_raw->sk_prot->sendmsg(sk_raw, &msg, skb->len);
		kfree_skb(skb);
	}
	set_fs(old_fs);
}

static DECLARE_WORK(delivery_work, pppopns_xmit_core);

static int pppopns_xmit(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct sock *sk_raw = (struct sock *)chan->private;
	struct pppopns_opt *opt = &pppox_sk(sk_raw->sk_user_data)->proto.pns;
	struct header *hdr;
	__u16 length;

	/* Install PPP address and control. */
	skb_push(skb, 2);
	skb->data[0] = PPP_ADDR;
	skb->data[1] = PPP_CTRL;
	length = skb->len;

	/* Install PPTP GRE header. */
	hdr = (struct header *)skb_push(skb, 12);
	hdr->bits = PPTP_GRE_BITS | PPTP_GRE_SEQ_BIT;
	hdr->type = PPTP_GRE_TYPE;
	hdr->length = htons(length);
	hdr->call = opt->remote;
	hdr->sequence = htonl(opt->xmit_sequence);
	opt->xmit_sequence++;

	/* Now send the packet via the delivery queue. */
	skb_set_owner_w(skb, sk_raw);
	skb_queue_tail(&delivery_queue, skb);
	schedule_work(&delivery_work);
	return 1;
}

/******************************************************************************/

static struct ppp_channel_ops pppopns_channel_ops = {
	.start_xmit = pppopns_xmit,
};

static int pppopns_connect(struct socket *sock, struct sockaddr *useraddr,
	int addrlen, int flags)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	struct sockaddr_pppopns *addr = (struct sockaddr_pppopns *)useraddr;
	struct sockaddr_storage ss;
	struct socket *sock_tcp = NULL;
	struct socket *sock_raw = NULL;
	struct sock *sk_tcp;
	struct sock *sk_raw;
	int error;

	if (addrlen != sizeof(struct sockaddr_pppopns))
		return -EINVAL;

	lock_sock(sk);
	error = -EALREADY;
	if (sk->sk_state != PPPOX_NONE)
		goto out;

	sock_tcp = sockfd_lookup(addr->tcp_socket, &error);
	if (!sock_tcp)
		goto out;
	sk_tcp = sock_tcp->sk;
	error = -EPROTONOSUPPORT;
	if (sk_tcp->sk_protocol != IPPROTO_TCP)
		goto out;
	addrlen = sizeof(struct sockaddr_storage);
	error = kernel_getpeername(sock_tcp, (struct sockaddr *)&ss, &addrlen);
	if (error)
		goto out;
	if (!sk_tcp->sk_bound_dev_if) {
		struct dst_entry *dst = sk_dst_get(sk_tcp);
		error = -ENODEV;
		if (!dst)
			goto out;
		sk_tcp->sk_bound_dev_if = dst->dev->ifindex;
		dst_release(dst);
	}

	error = sock_create(ss.ss_family, SOCK_RAW, IPPROTO_GRE, &sock_raw);
	if (error)
		goto out;
	sk_raw = sock_raw->sk;
	sk_raw->sk_bound_dev_if = sk_tcp->sk_bound_dev_if;
	error = kernel_connect(sock_raw, (struct sockaddr *)&ss, addrlen, 0);
	if (error)
		goto out;

	po->chan.hdrlen = 14;
	po->chan.private = sk_raw;
	po->chan.ops = &pppopns_channel_ops;
	po->chan.mtu = PPP_MRU - 80;
	po->proto.pns.local = addr->local;
	po->proto.pns.remote = addr->remote;
	po->proto.pns.data_ready = sk_raw->sk_data_ready;
	po->proto.pns.backlog_rcv = sk_raw->sk_backlog_rcv;

	error = ppp_register_channel(&po->chan);
	if (error)
		goto out;

	sk->sk_state = PPPOX_CONNECTED;
	lock_sock(sk_raw);
	sk_raw->sk_data_ready = pppopns_recv;
	sk_raw->sk_backlog_rcv = pppopns_recv_core;
	sk_raw->sk_user_data = sk;
	release_sock(sk_raw);
out:
	if (sock_tcp)
		sockfd_put(sock_tcp);
	if (error && sock_raw)
		sock_release(sock_raw);
	release_sock(sk);
	return error;
}

static int pppopns_release(struct socket *sock)
{
	struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
    unsigned long flags;

	if (!sk)
		return 0;

	lock_sock(sk);
	if (sock_flag(sk, SOCK_DEAD)) {
		release_sock(sk);
		return -EBADF;
	}

	if (po) {
		spin_lock_irqsave(&po->recv_queue_lock, flags);
		if (po && timer_pending( &po->recv_queue_timer )) {	    
			del_timer_sync( &po->recv_queue_timer );
		}
		spin_unlock_irqrestore(&po->recv_queue_lock, flags);
	}

	if (sk->sk_state != PPPOX_NONE) {
		struct sock *sk_raw = (struct sock *)pppox_sk(sk)->chan.private;
		lock_sock(sk_raw);
		skb_queue_purge(&sk->sk_receive_queue);
		pppox_unbind_sock(sk);
		sk_raw->sk_data_ready = pppox_sk(sk)->proto.pns.data_ready;
		sk_raw->sk_backlog_rcv = pppox_sk(sk)->proto.pns.backlog_rcv;
		sk_raw->sk_user_data = NULL;
		release_sock(sk_raw);
		sock_release(sk_raw->sk_socket);
	}

	sock_orphan(sk);
	sock->sk = NULL;
	release_sock(sk);
	sock_put(sk);
	return 0;
}

/******************************************************************************/

static struct proto pppopns_proto = {
	.name = "PPPOPNS",
	.owner = THIS_MODULE,
	.obj_size = sizeof(struct pppox_sock),
};

static struct proto_ops pppopns_proto_ops = {
	.family = PF_PPPOX,
	.owner = THIS_MODULE,
	.release = pppopns_release,
	.bind = sock_no_bind,
	.connect = pppopns_connect,
	.socketpair = sock_no_socketpair,
	.accept = sock_no_accept,
	.getname = sock_no_getname,
	.poll = sock_no_poll,
	.ioctl = pppox_ioctl,
	.listen = sock_no_listen,
	.shutdown = sock_no_shutdown,
	.setsockopt = sock_no_setsockopt,
	.getsockopt = sock_no_getsockopt,
	.sendmsg = sock_no_sendmsg,
	.recvmsg = sock_no_recvmsg,
	.mmap = sock_no_mmap,
};

static int pppopns_create(struct net *net, struct socket *sock, int kern)
{
	struct sock *sk;
	struct pppox_sock *po;
	struct pppopns_opt *opt;

	sk = sk_alloc(net, PF_PPPOX, GFP_KERNEL, &pppopns_proto, kern);
	if (!sk)
		return -ENOMEM;

	sock_init_data(sock, sk);
	sock->state = SS_UNCONNECTED;
	sock->ops = &pppopns_proto_ops;
	sk->sk_protocol = PX_PROTO_OPNS;
	sk->sk_state = PPPOX_NONE;

	po = pppox_sk(sk);
	opt = &po->proto.pns;
	opt->ppp_flags = SC_GRE_SEQ_CHK;
	init_timer(&po->recv_queue_timer);
	spin_lock_init(&po->recv_queue_lock);

	return 0;
}

static int pppopns_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{

        struct sock *sk = sock->sk;
	struct pppox_sock *po = pppox_sk(sk);
	struct pppopns_opt *opt = &po->proto.pns;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	int err = -ENOTTY, val;

	switch (cmd) {
	case PPPIOCGFLAGS:
		printk("Getting pppopns socket flags.\n");
		val = opt->ppp_flags;
		if (put_user(val, p))
			break;
		err = 0;
		break;
	case PPPIOCSFLAGS:
		printk("Setting pppopns socket flags.\n");
		if (get_user(val, p))
			break;
		opt->ppp_flags = val;
		err = 0;
		break;
	}
       
	return err;
}

/******************************************************************************/

static struct pppox_proto pppopns_pppox_proto = {
	.create = pppopns_create,
	.ioctl = pppopns_ioctl,
	.owner = THIS_MODULE,
};

static int __init pppopns_init(void)
{
	int error;

	error = proto_register(&pppopns_proto, 0);
	if (error)
		return error;

	error = register_pppox_proto(PX_PROTO_OPNS, &pppopns_pppox_proto);
	if (error)
		proto_unregister(&pppopns_proto);
	else
		skb_queue_head_init(&delivery_queue);
	return error;
}

static void __exit pppopns_exit(void)
{
	unregister_pppox_proto(PX_PROTO_OPNS);
	proto_unregister(&pppopns_proto);
}

module_init(pppopns_init);
module_exit(pppopns_exit);

MODULE_DESCRIPTION("PPP on PPTP Network Server (PPPoPNS)");
MODULE_AUTHOR("Chia-chi Yeh <chiachi@android.com>");
MODULE_LICENSE("GPL");
