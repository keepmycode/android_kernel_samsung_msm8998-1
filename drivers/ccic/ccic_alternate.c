/*
 * driver/ccic/ccic_alternate.c - S2MM005 USB CCIC Alternate mode driver
 *
 * Copyright (C) 2016 Samsung Electronics
 * Author: Wookwang Lee <wookwang.lee@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */
#include <linux/ccic/s2mm005.h>
#include <linux/ccic/s2mm005_ext.h>
#include <linux/ccic/ccic_alternate.h>
#if defined(CONFIG_USB_HOST_NOTIFY)
#include <linux/usb_notify.h>
#endif
/* switch device header */
#if defined(CONFIG_SWITCH)
#include <linux/switch.h>
#endif /* CONFIG_SWITCH */
#include <linux/usb_notify.h>
////////////////////////////////////////////////////////////////////////////////
// s2mm005_cc.c called s2mm005_alternate.c
////////////////////////////////////////////////////////////////////////////////
#if defined(CONFIG_CCIC_ALTERNATE_MODE)
extern struct device *ccic_device;
extern int dwc3_msm_is_suspended(void);
extern int dwc3_msm_is_host_highspeed(void);

#if defined(CONFIG_SWITCH)
static struct switch_dev switch_dock = {
	.name = "ccic_dock",
};
#endif /* CONFIG_SWITCH */

static char VDM_MSG_IRQ_State_Print[9][40] =
{
    {"bFLAG_Vdm_Reserve_b0"},
    {"bFLAG_Vdm_Discover_ID"},
    {"bFLAG_Vdm_Discover_SVIDs"},
    {"bFLAG_Vdm_Discover_MODEs"},
    {"bFLAG_Vdm_Enter_Mode"},
    {"bFLAG_Vdm_Exit_Mode"},
    {"bFLAG_Vdm_Attention"},
    {"bFlag_Vdm_DP_Status_Update"},
    {"bFlag_Vdm_DP_Configure"},
};

static char DP_Pin_Assignment_Print[7][40] =
{
    {"DP_Pin_Assignment_None"},
    {"DP_Pin_Assignment_A"},
    {"DP_Pin_Assignment_B"},
    {"DP_Pin_Assignment_C"},
    {"DP_Pin_Assignment_D"},
    {"DP_Pin_Assignment_E"},
    {"DP_Pin_Assignment_F"},

};
////////////////////////////////////////////////////////////////////////////////
// Alternate mode processing
////////////////////////////////////////////////////////////////////////////////
int ccic_register_switch_device(int mode)
{
#ifdef CONFIG_SWITCH
	int ret = 0;
	if (mode) {
		ret = switch_dev_register(&switch_dock);
		if (ret < 0) {
			pr_err("%s: Failed to register dock switch(%d)\n",
				__func__, ret);
			return -ENODEV;
		}
	} else {
		switch_dev_unregister(&switch_dock);
	}
#endif /* CONFIG_SWITCH */
	return 0;
}

static void ccic_send_dock_intent(int type)
{
	pr_info("%s: CCIC dock type(%d)\n", __func__, type);
#ifdef CONFIG_SWITCH
	switch_set_state(&switch_dock, type);
#endif /* CONFIG_SWITCH */
}

void acc_detach_check(struct work_struct *wk)
{
	struct delayed_work *delay_work =
		container_of(wk, struct delayed_work, work);
	struct s2mm005_data *usbpd_data =
		container_of(delay_work, struct s2mm005_data, acc_detach_work);

	pr_info("%s: usbpd_data->pd_state : %d\n", __func__, usbpd_data->pd_state);
	if (usbpd_data->pd_state == State_PE_Initial_detach) {
		if (usbpd_data->acc_type != CCIC_DOCK_DETACHED) {
			ccic_send_dock_intent(CCIC_DOCK_DETACHED);
			usbpd_data->acc_type = CCIC_DOCK_DETACHED;
		}
	}
}

void set_enable_powernego(int mode)
{
	struct s2mm005_data *usbpd_data;
	u8 W_DATA[2];

	if (!ccic_device)
		return;
	usbpd_data = dev_get_drvdata(ccic_device);
	if (!usbpd_data)
		return;

	if(mode) {
		W_DATA[0] = 0x3;
		W_DATA[1] = 0x42;
		s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);
		pr_info("%s : Power nego start\n", __func__);
	} else {
		W_DATA[0] = 0x3;
		W_DATA[1] = 0x42;
		s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);
		pr_info("%s : Power nego stop\n", __func__);
	}
}

void acc_detach_process(void *data)
{
	struct s2mm005_data *usbpd_data = data;
	CC_NOTI_TYPEDEF ccic_alt_noti;
	
	pr_info("%s: usbpd_data->pd_state : %d\n", __func__, usbpd_data->pd_state);
	ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
	ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
	ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_CONNECT;
	ccic_alt_noti.sub1 = CCIC_NOTIFY_DETACH;
	ccic_alt_noti.sub2 = 0;
	ccic_alt_noti.sub3 = 0;
	ccic_event_work(usbpd_data,ccic_alt_noti.dest,ccic_alt_noti.id,
				ccic_alt_noti.sub1,ccic_alt_noti.sub2,ccic_alt_noti.sub3);
	usbpd_data->acc_type = CCIC_DOCK_DETACHED;
}

void set_usb_phy_completion(int kind)
{
	struct s2mm005_data *usbpd_data;

	if(!ccic_device)
		return;
	usbpd_data = dev_get_drvdata(ccic_device);
	if(!usbpd_data)
		return;
	
	if(kind == 0){
		pr_info("%s : resume complete! \n", __func__);
		complete(&usbpd_data->resume_wait);
	}
	else{
		pr_info("%s : suspend complete! \n", __func__);
		complete(&usbpd_data->suspend_wait);
	}	
}

void set_enable_alternate_mode(int mode)
{
	struct s2mm005_data *usbpd_data;
	static int check_is_driver_loaded = 0;
	static int prev_alternate_mode = 0;
	u8 W_DATA[2];

	if (!ccic_device)
		return;
	usbpd_data = dev_get_drvdata(ccic_device);
	if (!usbpd_data)
		return;

#ifdef CONFIG_USB_NOTIFY_PROC_LOG
	store_usblog_notify(NOTIFY_ALTERNATEMODE, (void *)&mode, NULL);
#endif
	if ((mode & ALTERNATE_MODE_NOT_READY) && (mode & ALTERNATE_MODE_READY)) {
		pr_info("%s : mode is invalid! \n", __func__);
		return;
	}
	if ((mode & ALTERNATE_MODE_START) && (mode & ALTERNATE_MODE_STOP)) {
		pr_info("%s : mode is invalid! \n", __func__);
		return;
	}
	if (mode & ALTERNATE_MODE_RESET) {
		pr_info("%s : mode is reset! check_is_driver_loaded=%d, prev_alternate_mode=%d\n",
			__func__, check_is_driver_loaded, prev_alternate_mode);
		if (check_is_driver_loaded && (prev_alternate_mode == ALTERNATE_MODE_START)) {
			W_DATA[0] = 0x3;
			W_DATA[1] = 0x31;
			s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);
			pr_info("%s : alternate mode is reset as start! \n",	__func__);
			prev_alternate_mode = ALTERNATE_MODE_START;
			set_enable_powernego(1);
		} else if (check_is_driver_loaded && (prev_alternate_mode == ALTERNATE_MODE_STOP)) {
			W_DATA[0] = 0x3;
			W_DATA[1] = 0x33;
			s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);
			pr_info("%s : alternate mode is reset as stop! \n",__func__);
			prev_alternate_mode = ALTERNATE_MODE_STOP;
		} else
			;
	} else {
		if (mode & ALTERNATE_MODE_NOT_READY) {
			check_is_driver_loaded = 0;
			pr_info("%s : alternate mode is not ready! \n", __func__);
		} else if (mode & ALTERNATE_MODE_READY) {
			check_is_driver_loaded = 1;
			pr_info("%s : alternate mode is ready! \n", __func__);
		} else
			;

		if (check_is_driver_loaded) {
			if (mode & ALTERNATE_MODE_START) {
				W_DATA[0] = 0x3;
				W_DATA[1] = 0x31;
				s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);
				pr_info("%s : alternate mode is started! \n",	__func__);
				prev_alternate_mode = ALTERNATE_MODE_START;
				set_enable_powernego(1);
			} else if(mode & ALTERNATE_MODE_STOP) {
				W_DATA[0] = 0x3;
				W_DATA[1] = 0x33;
				s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);
				pr_info("%s : alternate mode is stopped! \n",__func__);
				prev_alternate_mode = ALTERNATE_MODE_STOP;
			}
		}
	}
	return;
}

void set_clear_discover_mode(void)
{
	struct s2mm005_data *usbpd_data;
	u8 W_DATA[2];

	if(!ccic_device)
		return;
	usbpd_data = dev_get_drvdata(ccic_device);
	if(!usbpd_data)
		return;

	W_DATA[0] =0x3;
	W_DATA[1] =0x32;

	s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 2);

	pr_info("%s : clear discover mode! \n", __func__);
}

void set_host_turn_on_event(int mode)
{
	struct s2mm005_data *usbpd_data;

	if(!ccic_device)
		return;
	usbpd_data = dev_get_drvdata(ccic_device);
	if(!usbpd_data)
		return;

	pr_info("%s : current_set is %d! \n", __func__, mode);
	if(mode) {
		usbpd_data->host_turn_on_event = 1;
		wake_up_interruptible(&usbpd_data->host_turn_on_wait_q);
	}
	else {
		usbpd_data->host_turn_on_event = 0;
	}
}

// temporarily add this function for DP TG request
void sbu_check(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	u8 W_DATA[4];
	u8 R_DATA;

	if (!usbpd_data) {
		pr_err("usbpd_data is NULL\n");
		return;
	}	

	W_DATA[0] =0x2;
	W_DATA[1] =0x10;

	// 0:Open_Drain  1:CMOS
	W_DATA[2] =0x74;
	W_DATA[3] =0x10;

	s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 4);
	s2mm005_read_byte(usbpd_data->i2c, 0x14, &R_DATA, 1);

	pr_err("%s CMOS SBU1 mode = %2x , SBU2 mode = %2x \n", __func__,
	   (R_DATA & 0x10) >> 4,(R_DATA & 0x20) >> 5);  // it shoud be 0
	

	W_DATA[0] =0x2;
	W_DATA[1] =0x10;

	// 0:Input Mode  1:Output Mode
	W_DATA[2] =0x78;
	W_DATA[3] =0x10;

	s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 4);
	s2mm005_read_byte(usbpd_data->i2c, 0x14, &R_DATA, 1);
	
	pr_err("%s Output Mode SBU1 status = %2x , SBU2 status = %2x \n", __func__,
	   (R_DATA & 0x10) >> 4,(R_DATA & 0x20) >> 5); // it shoud be 1


	W_DATA[0] =0x2;
	W_DATA[1] =0x10;

	// 0:Out Low   1:Out High
	W_DATA[2] =0x80;
	W_DATA[3] =0x10;

	s2mm005_write_byte(usbpd_data->i2c, 0x10, &W_DATA[0], 4);
	s2mm005_read_byte(usbpd_data->i2c, 0x14, &R_DATA, 1);
	
	pr_err("%s Low/High Check SBU1 status = %2x , SBU2 status = %2x \n", __func__,
	   (R_DATA & 0x10) >> 4,(R_DATA & 0x20) >> 5); // it shoud be 1	   
	
	return;
}

int get_diplayport_status(void)
{
	struct s2mm005_data *usbpd_data;
	if(!ccic_device)
		return 0;
	usbpd_data = dev_get_drvdata(ccic_device);
	if(!usbpd_data)
		return 0;

	pr_info("%s : current dp status %d! \n", __func__, usbpd_data->dp_is_connect);
	return usbpd_data->dp_is_connect;
}

static int process_check_accessory(void * data)
{
	struct s2mm005_data *usbpd_data = data;
#if defined(CONFIG_USB_HOST_NOTIFY) && defined(CONFIG_USB_HW_PARAM)
	struct otg_notify *o_notify = get_otg_notify();
#endif
	uint16_t vid = usbpd_data->Vendor_ID;
	uint16_t pid = usbpd_data->Product_ID;
	uint16_t dock_type = 0;

	/* detect Gear VR */
	if (usbpd_data->acc_type == CCIC_DOCK_DETACHED) {
		if (vid == SAMSUNG_VENDOR_ID) {
			switch (pid) {
			/* GearVR: Reserved GearVR PID+6 */
			case GEARVR_PRODUCT_ID:
			case GEARVR_PRODUCT_ID_1:
			case GEARVR_PRODUCT_ID_2:
			case GEARVR_PRODUCT_ID_3:
			case GEARVR_PRODUCT_ID_4:
			case GEARVR_PRODUCT_ID_5:
				dock_type = CCIC_DOCK_HMT;
				pr_info("%s : Samsung Gear VR connected.\n", __func__);
#if defined(CONFIG_USB_HOST_NOTIFY) && defined(CONFIG_USB_HW_PARAM)
				if (o_notify)
					inc_hw_param(o_notify, USB_CCIC_VR_USE_COUNT);
#endif
				break;
			case DEXDOCK_PRODUCT_ID:
				dock_type = CCIC_DOCK_DEX;
				pr_info("%s : Samsung DEX connected.\n", __func__);
#if defined(CONFIG_USB_HOST_NOTIFY) && defined(CONFIG_USB_HW_PARAM)
				if (o_notify)
					inc_hw_param(o_notify, USB_CCIC_DEX_USE_COUNT);
#endif

				break;
			case HDMI_PRODUCT_ID:
				dock_type = CCIC_DOCK_HDMI;
				pr_info("%s : Samsung HDMI connected.\n", __func__);
				break;
			default:
				break;
			}
		} else if (vid == SAMSUNG_MPA_VENDOR_ID) {
			switch(pid) {
			case MPA_PRODUCT_ID:
				dock_type = CCIC_DOCK_MPA;
				pr_info("%s : Samsung MPA connected.\n", __func__);
				break;
			default:
				break;
			}
		}
	}

	if (dock_type) {
		usbpd_data->acc_type = dock_type;
		ccic_send_dock_intent(dock_type);
		return 1;
	}
	return 0;
}

static int process_discover_identity(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_ID;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;
	
	// Message Type Definition
	U_DATA_MSG_ID_HEADER_Type     *DATA_MSG_ID = (U_DATA_MSG_ID_HEADER_Type *)&ReadMSG[8];
	U_PRODUCT_VDO_Type			  *DATA_MSG_PRODUCT = (U_PRODUCT_VDO_Type *)&ReadMSG[16];
	
	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return ret;
	}

	usbpd_data->is_samsung_accessory_enter_mode = 0;
	usbpd_data->is_sent_pin_configuration = 0;
	usbpd_data->Vendor_ID = DATA_MSG_ID->BITS.USB_Vendor_ID;
	usbpd_data->Product_ID = DATA_MSG_PRODUCT->BITS.Product_ID;
	usbpd_data->Device_Version = DATA_MSG_PRODUCT->BITS.Device_Version;	

	dev_info(&i2c->dev, "%s Vendor_ID : 0x%X, Product_ID : 0x%X Device Version 0x%X \n",\
		 __func__, usbpd_data->Vendor_ID, usbpd_data->Product_ID, usbpd_data->Device_Version);
	if (process_check_accessory(usbpd_data))
		dev_info(&i2c->dev, "%s : Samsung Accessory Connected.\n", __func__);
	return 0;
}

static int process_discover_svids(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_SVID;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;
#if defined(CONFIG_USB_HOST_NOTIFY) && defined(CONFIG_USB_HW_PARAM)	
	struct otg_notify *o_notify = get_otg_notify();
#endif
	CC_NOTI_TYPEDEF ccic_alt_noti;
	// Message Type Definition
	U_VDO1_Type 				  *DATA_MSG_VDO1 = (U_VDO1_Type *)&ReadMSG[8];
#if defined(CONFIG_USB_HOST_NOTIFY)
	uint timeleft = 0;
#endif

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return ret;
	}

	usbpd_data->SVID_0 = DATA_MSG_VDO1->BITS.SVID_0;
	usbpd_data->SVID_1 = DATA_MSG_VDO1->BITS.SVID_1;

	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {		
		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_CONNECT;
		ccic_alt_noti.sub1 = CCIC_NOTIFY_ATTACH;
		ccic_alt_noti.sub2 = usbpd_data->Vendor_ID;
		ccic_alt_noti.sub3 = usbpd_data->Product_ID;

#if defined(CONFIG_CCIC_ALTERNATE_MODE)
		if (usbpd_data->is_host == HOST_ON_BY_RD) {
			timeleft = wait_event_interruptible_timeout(usbpd_data->host_turn_on_wait_q,
					usbpd_data->host_turn_on_event, (usbpd_data->host_turn_on_wait_time)*HZ);
			dev_info(&i2c->dev, "%s host turn on wait = %d \n", __func__, timeleft);
			dev_info(&i2c->dev, "%s reinit suspend wait start\n", __func__);			
			reinit_completion(&usbpd_data->suspend_wait);			
			ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 0/*attach*/, USB_STATUS_NOTIFY_DETACH/*drp*/, 0);
			usbpd_data->is_host = HOST_OFF;

			if(!dwc3_msm_is_suspended())
			{
				timeleft = wait_for_completion_interruptible_timeout(&usbpd_data->suspend_wait,
							  msecs_to_jiffies(USB_PHY_SUSPEND_WAIT_MS));
				dev_info(&i2c->dev, "%s suspend_wait timeleft = %d \n", __func__, timeleft);
			}
		}
		
		if (usbpd_data->is_host == HOST_OFF) {
			ccic_event_work(usbpd_data,
					CCIC_NOTIFY_DEV_MUIC, CCIC_NOTIFY_ID_ATTACH, 1/*attach*/, 1/*rprd*/,0);
			usbpd_data->is_host = HOST_ON_BY_RD;
			dev_info(&i2c->dev, "%s reinit resume wait start\n", __func__);			
			reinit_completion(&usbpd_data->resume_wait);
			ccic_event_work(usbpd_data,			
					CCIC_NOTIFY_DEV_USB, CCIC_NOTIFY_ID_USB, 1/*attach*/, USB_STATUS_NOTIFY_ATTACH_DFP/*drp*/, 1/*high speed mode*/);
			timeleft = wait_for_completion_interruptible_timeout(&usbpd_data->resume_wait,
						  msecs_to_jiffies(USB_PHY_RESUME_WAIT_MS));
			dev_info(&i2c->dev, "%s resume_wait timeleft = %d \n", __func__, timeleft);
		}
		if( !dwc3_msm_is_host_highspeed() && usbpd_data->host_turn_on_event) {
			dev_info(&i2c->dev, "%s dp is ready but usb phy isn't high speed! Stop the displary port! \n", __func__);
			return -1;
		}
		usbpd_data->dp_is_connect = 1;
#if defined(CONFIG_USB_HOST_NOTIFY) && defined(CONFIG_USB_HW_PARAM)
		if (o_notify)			
			inc_hw_param(o_notify, USB_CCIC_DP_USE_COUNT);
#endif		
#endif
		ccic_event_work(usbpd_data,ccic_alt_noti.dest,ccic_alt_noti.id,
			ccic_alt_noti.sub1,ccic_alt_noti.sub2,ccic_alt_noti.sub3);
	}

	dev_info(&i2c->dev, "%s SVID_0 : 0x%X, SVID_1 : 0x%X\n", __func__, usbpd_data->SVID_0, usbpd_data->SVID_1);
	return 0;
}

static int process_discover_modes(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_MODE;
	uint8_t ReadMSG[32] = {0,};
	uint8_t W_DATA[3] = {0,};
	int ret = 0;
	DIS_MODE_DP_CAPA_Type *pDP_DIS_MODE = (DIS_MODE_DP_CAPA_Type *)&ReadMSG[0];
	U_DATA_MSG_VDM_HEADER_Type *DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type*)&ReadMSG[4];

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return ret;
	}

	dev_info(&i2c->dev, "%s : vendor_id = 0x%04x , svid_1 = 0x%04x\n", __func__,DATA_MSG_VDM->BITS.Standard_Vendor_ID,usbpd_data->SVID_1);
	if(DATA_MSG_VDM->BITS.Standard_Vendor_ID == TypeC_DP_SUPPORT && usbpd_data->SVID_0 == TypeC_DP_SUPPORT) {
		//  pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.
		dev_info(&i2c->dev,"pDP_DIS_MODE->MSG_HEADER.DATA = 0x%08X\n\r",pDP_DIS_MODE->MSG_HEADER.DATA);
		dev_info(&i2c->dev,"pDP_DIS_MODE->DATA_MSG_VDM_HEADER.DATA = 0x%08X\n\r",pDP_DIS_MODE->DATA_MSG_VDM_HEADER.DATA);
		dev_info(&i2c->dev,"pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.DATA = 0x%08X\n\r",pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.DATA);

		if(pDP_DIS_MODE->MSG_HEADER.BITS.Number_of_obj > 1) {
			if( ( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_UFP_D_Capable)
				  && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_Receptacle) )
				||	( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_DFP_D_Capable)
				  && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_PLUG) ) ) {

				usbpd_data->pin_assignment = pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.UFP_D_Pin_Assignments;
				dev_info(&i2c->dev, "%s support UFP_D 0x%08x\n",
					 __func__, usbpd_data->pin_assignment);
			} else if( ( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_DFP_D_Capable)
					 && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_Receptacle) )
				   ||  ( (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_UFP_D_Capable)
					 && (pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_PLUG) ) ) {

				usbpd_data->pin_assignment	= pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.DFP_D_Pin_Assignments;
				dev_info(&i2c->dev, "%s support DFP_D 0x%08x\n",
					 __func__, usbpd_data->pin_assignment);
			}
			else if( pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Port_Capability == num_DFP_D_and_UFP_D_Capable ) {
				if(pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.Receptacle_Indication == num_USB_TYPE_C_PLUG){
					usbpd_data->pin_assignment	= pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.DFP_D_Pin_Assignments;
					dev_info(&i2c->dev, "%s support DFP_D 0x%08x\n",__func__, usbpd_data->pin_assignment);
				}
				else{
					usbpd_data->pin_assignment	= pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.UFP_D_Pin_Assignments;
					dev_info(&i2c->dev, "%s support UFP_D 0x%08x\n",__func__, usbpd_data->pin_assignment);
				}
			}
			else{
				usbpd_data->pin_assignment = DP_PIN_ASSIGNMENT_NODE;
				dev_info(&i2c->dev, "%s there is not valid object information!!! \n",__func__);
			}
		}
	}

	if(DATA_MSG_VDM->BITS.Standard_Vendor_ID == TypeC_Dex_SUPPORT && usbpd_data->SVID_1 == TypeC_Dex_SUPPORT) {
		dev_info(&i2c->dev, "%s : dex mode discover_mode ack status!\n", __func__);
		//  pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.BITS.
		dev_info(&i2c->dev,"pDP_DIS_MODE->MSG_HEADER.DATA = 0x%08X\n\r",pDP_DIS_MODE->MSG_HEADER.DATA);
		dev_info(&i2c->dev,"pDP_DIS_MODE->DATA_MSG_VDM_HEADER.DATA = 0x%08X\n\r",pDP_DIS_MODE->DATA_MSG_VDM_HEADER.DATA);
		dev_info(&i2c->dev,"pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.DATA = 0x%08X\n\r",pDP_DIS_MODE->DATA_MSG_MODE_VDO_DP.DATA);		

		REG_ADD = REG_I2C_SLV_CMD;
		W_DATA[0] = MODE_INTERFACE;	/* Mode Interface */
		W_DATA[1] = PD_NEXT_STATE;   /* Select mode as pd next state*/
		W_DATA[2] = 89;	/* PD next state*/
		ret = s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3 );
		if (ret < 0) {
			dev_err(&i2c->dev, "%s has i2c write error.\n",
				__func__);
			return ret;
		}
	}
	
	dev_info(&i2c->dev, "%s\n", __func__);
	set_clear_discover_mode();
	return 0;
}

static int process_enter_mode(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD;
	uint8_t ReadMSG[32] = {0,};
	int ret = 0;

	U_DATA_MSG_VDM_HEADER_Type *DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type*)&ReadMSG[4];

	dev_info(&i2c->dev, "%s\n", __func__);

	REG_ADD = REG_RX_ENTER_MODE;
	/* Message Type Definition */
	DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type *)&ReadMSG[4];
	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c read error.\n", __func__);
		return ret;
	}

	if (DATA_MSG_VDM->BITS.VDM_command_type == 1) {
		dev_info(&i2c->dev, "%s : EnterMode ACK.\n", __func__);
		if(DATA_MSG_VDM->BITS.Standard_Vendor_ID == TypeC_Dex_SUPPORT && usbpd_data->SVID_1 == TypeC_Dex_SUPPORT) {
			usbpd_data->is_samsung_accessory_enter_mode = 1;
			dev_info(&i2c->dev, "%s : dex mode enter_mode ack status!\n", __func__);		
		}
	}
	else
		dev_info(&i2c->dev, "%s : EnterMode NAK.\n", __func__);

	return 0;
}

static int process_attention(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_ATTENTION;
	uint8_t ReadMSG[32] = {0,};
	uint8_t W_DATA[3] = {0,};	
	int ret = 0;
	int i;
	CC_NOTI_TYPEDEF ccic_alt_noti;
	VDO_MESSAGE_Type *VDO_MSG;
	DIS_ATTENTION_MESSAGE_DP_STATUS_Type *DP_ATTENTION;
	u8 multi_func_preference = 0;	

	pr_info("%s \n",__func__);
	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return ret;
	}

	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {
		DP_ATTENTION = (DIS_ATTENTION_MESSAGE_DP_STATUS_Type *)&ReadMSG[0];

		dev_info(&i2c->dev, "%s DP_ATTENTION = 0x%08X\n", __func__,
			 DP_ATTENTION->DATA_MSG_DP_STATUS.DATA);

		if(usbpd_data->is_sent_pin_configuration == 0) {
			/* 1. Pin Assignment */
			REG_ADD = 0x10;
			W_DATA[0] = MODE_INTERFACE; /* Mode Interface */
			W_DATA[1] = DP_ALT_MODE_REQ;   /* DP Alternate Mode Request */
			W_DATA[2] = 0;	/* DP Pin Assign select */

			multi_func_preference =
				DP_ATTENTION->DATA_MSG_DP_STATUS.BITS.Multi_Function_Preference;
			if(multi_func_preference == 1) {
				if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_D) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_D;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_D;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_B) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_B;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_B;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_F) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_F;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_F;
				} else {
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_UNKNOWN;
					pr_info("wrong pin assignment value\n");
				}
			} else {
				if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_C) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_C;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_C;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_E) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_E;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_E;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_A) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_A;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_A;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_D) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_D;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_D;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_B) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_B;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_B;
				} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_F) {
					W_DATA[2] =DP_PIN_ASSIGNMENT_F;
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_F;
				} else {
					ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_UNKNOWN;
					pr_info("wrong pin assignment value\n");
				}
			}

			usbpd_data->selected_pin = ccic_alt_noti.sub1;
			dev_info(&i2c->dev, "%s %s\n", __func__,
				 DP_Pin_Assignment_Print[ccic_alt_noti.sub1]);
			
			sbu_check(usbpd_data);
			
			ret = s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3 );
			if (ret < 0) {
				dev_err(&i2c->dev, "%s has i2c write error.\n",
					__func__);
				return ret;
			}
			usbpd_data->is_sent_pin_configuration = 1;
		} else {
			dev_info(&i2c->dev, "%s : pin configuration is already sent as %s!\n", __func__,
				DP_Pin_Assignment_Print[usbpd_data->selected_pin]);
		}

		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_HPD;
		ccic_alt_noti.sub1 = CCIC_NOTIFY_LOW;	/*HPD_STATE*/
		ccic_alt_noti.sub2 = 0;			/*HPD IRQ*/
		ccic_alt_noti.sub3 = 0;

		if ( DP_ATTENTION->DATA_MSG_DP_STATUS.BITS.HPD_State == 1) {
			ccic_alt_noti.sub1 = CCIC_NOTIFY_HIGH;
		} else if(DP_ATTENTION->DATA_MSG_DP_STATUS.BITS.HPD_State == 0){
			ccic_alt_noti.sub1 = CCIC_NOTIFY_LOW;
		}
		if(DP_ATTENTION->DATA_MSG_DP_STATUS.BITS.HPD_Interrupt == 1) {
			ccic_alt_noti.sub2 = CCIC_NOTIFY_IRQ;
		}
		ccic_event_work(usbpd_data,ccic_alt_noti.dest,ccic_alt_noti.id,
			ccic_alt_noti.sub1,ccic_alt_noti.sub2,ccic_alt_noti.sub3);
	} else {
		VDO_MSG = (VDO_MESSAGE_Type *)&ReadMSG[8];

		for(i=0;i<6;i++)
			dev_info(&i2c->dev, "%s : VDO_%d : %d\n", __func__,
				 i+1, VDO_MSG->VDO[i]);
	}
	return 0;

}

static int process_dp_status_update(void *data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_DP_STATUS_UPDATE;
	uint8_t ReadMSG[32] = {0,};
	uint8_t W_DATA[3] = {0,};
	int ret = 0;
	int i;
	u8 multi_func_preference = 0;

	CC_NOTI_TYPEDEF ccic_alt_noti;
	VDO_MESSAGE_Type *VDO_MSG;
	DP_STATUS_UPDATE_Type *DP_STATUS;

	pr_info("%s \n",__func__);
	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return ret;
	}

	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {
		DP_STATUS = (DP_STATUS_UPDATE_Type *)&ReadMSG[0];

		dev_info(&i2c->dev, "%s DP_STATUS_UPDATE = 0x%08X\n", __func__,
			 DP_STATUS->DATA_DP_STATUS_UPDATE.DATA);

		if(DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.Port_Connected == 0x00) {
			dev_info(&i2c->dev, "%s : port disconnected!\n", __func__);			
		}
		else {
			if(usbpd_data->is_sent_pin_configuration == 0) {		
				/* 1. Pin Assignment */
				REG_ADD = 0x10;
				W_DATA[0] = MODE_INTERFACE;	/* Mode Interface */
				W_DATA[1] = DP_ALT_MODE_REQ;   /* DP Alternate Mode Request */
				W_DATA[2] = 0;	/* DP Pin Assign select */

				multi_func_preference =
					DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.Multi_Function_Preference;
				if(multi_func_preference == 1) {
					if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_D) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_D;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_D;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_B) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_B;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_B;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_F) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_F;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_F;
					} else {
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_UNKNOWN;
						pr_info("wrong pin assignment value\n");
					}
				} else {
					if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_C) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_C;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_C;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_E) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_E;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_E;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_A) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_A;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_A;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_D) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_D;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_D;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_B) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_B;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_B;
					} else if(usbpd_data->pin_assignment & DP_PIN_ASSIGNMENT_F) {
						W_DATA[2] =DP_PIN_ASSIGNMENT_F;
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_F;
					} else {
						ccic_alt_noti.sub1 = CCIC_NOTIFY_DP_PIN_UNKNOWN;
						pr_info("wrong pin assignment value\n");
					}
				}

				usbpd_data->selected_pin = ccic_alt_noti.sub1;
				dev_info(&i2c->dev, "%s %s\n", __func__,
					 DP_Pin_Assignment_Print[ccic_alt_noti.sub1]);
				
				sbu_check(usbpd_data);
				
				ret = s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3 );
				if (ret < 0) {
					dev_err(&i2c->dev, "%s has i2c write error.\n",
						__func__);
					return ret;
				}
				usbpd_data->is_sent_pin_configuration = 1;
			} else {
				dev_info(&i2c->dev, "%s : pin configuration is already sent as %s!\n", __func__,
					DP_Pin_Assignment_Print[usbpd_data->selected_pin]);
			}
		}

		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_HPD;
		ccic_alt_noti.sub1 = CCIC_NOTIFY_LOW;	/*HPD_STATE*/
		ccic_alt_noti.sub2 = 0;			/*HPD IRQ*/
		ccic_alt_noti.sub3 = 0;

		if ( DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.HPD_State == 1) {
			ccic_alt_noti.sub1 = CCIC_NOTIFY_HIGH;
		} else if(DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.HPD_State == 0){
			ccic_alt_noti.sub1 = CCIC_NOTIFY_LOW;
		}
		if(DP_STATUS->DATA_DP_STATUS_UPDATE.BITS.HPD_Interrupt == 1) {
			ccic_alt_noti.sub2 = CCIC_NOTIFY_IRQ;
		}
		ccic_event_work(usbpd_data,ccic_alt_noti.dest,ccic_alt_noti.id,
			ccic_alt_noti.sub1,ccic_alt_noti.sub2,ccic_alt_noti.sub3);
	}else {
		VDO_MSG = (VDO_MESSAGE_Type *)&ReadMSG[8];

		for(i=0;i<6;i++)
			dev_info(&i2c->dev, "%s : VDO_%d : %d\n", __func__,
				 i+1, VDO_MSG->VDO[i]);
	}
	return 0;
}

static int process_dp_configure(void *data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_RX_DIS_DP_CONFIGURE;
	uint8_t ReadMSG[32] = {0,};
	uint8_t W_DATA[3] = {0,};	
	int ret = 0;
	U_DATA_MSG_VDM_HEADER_Type *DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type*)&ReadMSG[4];
	CC_NOTI_TYPEDEF ccic_alt_noti;	
	
	dev_info(&i2c->dev, "%s\n", __func__);

	ret = s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	if (ret < 0) {
		dev_err(&i2c->dev, "%s has i2c error.\n", __func__);
		return ret;
	}

	dev_info(&i2c->dev, "%s : vendor_id = 0x%04x , svid_1 = 0x%04x\n", __func__,DATA_MSG_VDM->BITS.Standard_Vendor_ID,usbpd_data->SVID_1);
	if( usbpd_data->SVID_0 == TypeC_DP_SUPPORT ) {
		ccic_alt_noti.src = CCIC_NOTIFY_DEV_CCIC;
		ccic_alt_noti.dest = CCIC_NOTIFY_DEV_DP;
		ccic_alt_noti.id = CCIC_NOTIFY_ID_DP_LINK_CONF;
		ccic_alt_noti.sub1 = usbpd_data->selected_pin;
		ccic_alt_noti.sub2 = 0;			/*HPD IRQ*/
		ccic_alt_noti.sub3 = 0;
		ccic_event_work(usbpd_data,ccic_alt_noti.dest,ccic_alt_noti.id,
			ccic_alt_noti.sub1,ccic_alt_noti.sub2,ccic_alt_noti.sub3);
	}
	
	if(DATA_MSG_VDM->BITS.Standard_Vendor_ID == TypeC_DP_SUPPORT && usbpd_data->SVID_1 == TypeC_Dex_SUPPORT){
		/* write s2mm005 with TypeC_Dex_SUPPORT SVID */
		/* It will start discover mode with that svid */
		dev_info(&i2c->dev, "%s : svid1 is dex station\n", __func__);
		REG_ADD = REG_I2C_SLV_CMD;
		W_DATA[0] = MODE_INTERFACE;	/* Mode Interface */
		W_DATA[1] = SVID_SELECT;   /* SVID select*/
		W_DATA[2] = 1;	/* SVID select with Samsung vendor ID*/
		ret = s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3 );
		if (ret < 0) {
			dev_err(&i2c->dev, "%s has i2c write error.\n",
				__func__);
			return ret;
		}		
	}

	return 0;
}

static void process_alternate_mode(void * data)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint32_t mode = usbpd_data->alternate_state;
	int	ret = 0;
	struct otg_notify *o_notify = get_otg_notify();
	if (mode) {
		dev_info(&i2c->dev, "%s, mode : 0x%x\n", __func__, mode);
		if (o_notify != NULL && is_blocked(o_notify, NOTIFY_BLOCK_TYPE_HOST)) {
			dev_info(&i2c->dev, "%s, host is blocked, skip all the alternate mode.\n", __func__);
			goto process_error;
		}
		if (mode & VDM_DISCOVER_ID)
			ret = process_discover_identity(usbpd_data);
		if(ret)
			goto process_error;
		if (mode & VDM_DISCOVER_SVIDS)
			ret = process_discover_svids(usbpd_data);
		if(ret)
			goto process_error;
		if (mode & VDM_DISCOVER_MODES)
			ret = process_discover_modes(usbpd_data);
		if(ret)
			goto process_error;		
		if (mode & VDM_ENTER_MODE)
			ret = process_enter_mode(usbpd_data);
		if(ret)
			goto process_error;		
		if (mode & VDM_DP_STATUS_UPDATE)
			ret = process_dp_status_update(usbpd_data);
		if(ret)
			goto process_error;		
		if (mode & VDM_DP_CONFIGURE)
			ret = process_dp_configure(usbpd_data);
		if(ret)
			goto process_error;		
		if (mode & VDM_ATTENTION)
			ret = process_attention(usbpd_data);		
process_error:		
		usbpd_data->alternate_state = 0;
	}
}

void send_alternate_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_VDM_MSG_REQ;
	u8 mode = (u8)cmd;
	dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[cmd][0]);
	s2mm005_write_byte(i2c, REG_ADD, &mode, 1);
}

void receive_alternate_message(void * data, VDM_MSG_IRQ_STATUS_Type *VDM_MSG_IRQ_State)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;

	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Discover_ID) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[1][0]);
		usbpd_data->alternate_state |= VDM_DISCOVER_ID;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Discover_SVIDs) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[2][0]);
		usbpd_data->alternate_state |= VDM_DISCOVER_SVIDS;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Discover_MODEs) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[3][0]);
		usbpd_data->alternate_state |= VDM_DISCOVER_MODES;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Enter_Mode) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[4][0]);
		usbpd_data->alternate_state |= VDM_ENTER_MODE;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Exit_Mode) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[5][0]);
		usbpd_data->alternate_state |= VDM_EXIT_MODE;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_Attention) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[6][0]);
		usbpd_data->alternate_state |= VDM_ATTENTION;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_DP_Status_Update) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[7][0]);
		usbpd_data->alternate_state |= VDM_DP_STATUS_UPDATE;
	}
	if (VDM_MSG_IRQ_State->BITS.Vdm_Flag_DP_Configure) {
		dev_info(&i2c->dev, "%s : %s\n",__func__, &VDM_MSG_IRQ_State_Print[8][0]);
		usbpd_data->alternate_state |= VDM_DP_CONFIGURE;
	}
	
	process_alternate_mode(usbpd_data);
}

int send_samsung_unstructured_vdm_message(void * data, const char *buf, size_t size)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_SEND;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[2];
	// Message Type Definition
	MSG_HEADER_Type *MSG_HDR;
	U_UNSTRUCTURED_VDM_HEADER_Type	*UVDM_HEADER;
	U_SEC_UNSTRUCTURED_VDM_HEADER_Type *SEND_SEC_VDM_HEADER;
	U_SEC_UNSTRUCTURED_VDM_HEADER_Type *RECEIVED_VDM_HEADER;
#if 0
	char *received_data;
	int sret = 0;
#else
	int received_data;
#endif
#if 0 //TEMP
	
	U_SEC_UNSTRUCTURED_VDM_HEADER_Type temp_test;

	dev_info(&i2c->dev, "%s - enter!\n", __func__);
	
	temp_test.BITS.PID = DEXDOCK_PRODUCT_ID;
	temp_test.BITS.DATA_TYPE = SEC_UVDM_SHORT_DATA;
	temp_test.BITS.COMMAND_TYPE = SEC_UVDM_ININIATOR;
	sscanf(buf, "%d", &mode);
	temp_test.BITS.DATA = mode;
	
	RECEIVED_VDM_HEADER = (U_SEC_UNSTRUCTURED_VDM_HEADER_Type*)&temp_test;
	MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	UVDM_HEADER = (U_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[4];
	SEND_SEC_VDM_HEADER = (VDO_MESSAGE_Type *)&SendMSG[8];

	// process common data
	MSG_HDR->Message_Type = 15; // send VDM message
	UVDM_HEADER->BITS.USB_Vendor_ID = SAMSUNG_VENDOR_ID; // VID
	UVDM_HEADER->BITS.VENDOR_DEFINED_MESSAGE = SEC_UVDM_UNSTRUCTURED_VDM;

	*SEND_SEC_VDM_HEADER = *RECEIVED_VDM_HEADER;
#else
	if(buf == NULL || size < sizeof(U_SEC_UNSTRUCTURED_VDM_HEADER_Type))
	{
		dev_info(&i2c->dev, "%s given data is not valid !\n", __func__);
		return -EINVAL;
	}

	if(!usbpd_data->is_samsung_accessory_enter_mode) {
		dev_info(&i2c->dev, "%s - samsung_accessory mode is not ready!\n", __func__);
		return -ENXIO;
	}
#if 0
	received_data = kzalloc(size+1, GFP_KERNEL);
	if (!received_data)
		goto error;
	
	sret = sscanf(buf, "%s", received_data);
		if (sret != 1)
			goto error1;
#else
	sscanf(buf, "%d", &received_data);
#endif

	RECEIVED_VDM_HEADER = (U_SEC_UNSTRUCTURED_VDM_HEADER_Type*)&received_data;
	MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	UVDM_HEADER = (U_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[4];
	SEND_SEC_VDM_HEADER = (U_SEC_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[8];

	// process common data
	MSG_HDR->Message_Type = 15; // send VDM message
	UVDM_HEADER->BITS.USB_Vendor_ID = SAMSUNG_VENDOR_ID; // VID
	UVDM_HEADER->BITS.VENDOR_DEFINED_MESSAGE = SEC_UVDM_UNSTRUCTURED_VDM;

	*SEND_SEC_VDM_HEADER = *RECEIVED_VDM_HEADER;
#endif
	if(RECEIVED_VDM_HEADER->BITS.DATA_TYPE == SEC_UVDM_SHORT_DATA)
	{	
		dev_info(&i2c->dev, "%s - process short data!\n", __func__);
		// process short data 
		// phase 1. fill message header
		MSG_HDR->Number_of_obj = 2; // VDM Header + 6 VDOs = MAX 7
		// phase 2. fill uvdm header (already filled)		
		// phase 3. fill sec uvdm header
		SEND_SEC_VDM_HEADER->BITS.TOTAL_NUMBER_OF_UVDM_SET = 1;		
	}
	else
	{
		dev_info(&i2c->dev, "%s - process long data!\n", __func__);
		// process long data
		// phase 1. fill message header
		// phase 2. fill uvdm header
		// phase 3. fill sec uvdm header
		// phase 4.5.6.7 fill sec data header , data , sec data tail and so on.
	}

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	// send uVDM message
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = SEL_SSM_MSG_REQ;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	if(RECEIVED_VDM_HEADER->BITS.DATA_TYPE == SEC_UVDM_SHORT_DATA)
	{
		dev_info(&i2c->dev, "%s - exit : short data transfer complete!\n", __func__);
	}
#if 0	
error1:
	kfree(received_data);	
error:
	return;	
#endif
	return 0;
}

void send_unstructured_vdm_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_SEND;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[2];
	uint32_t message = (uint32_t)cmd;
	int i;

	// Message Type Definition
	MSG_HEADER_Type *MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	U_UNSTRUCTURED_VDM_HEADER_Type	*DATA_MSG_UVDM = (U_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[4];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&SendMSG[8];

	// fill message
	MSG_HDR->Message_Type = 15; // send VDM message
	MSG_HDR->Number_of_obj = 7; // VDM Header + 6 VDOs = MAX 7
	
	DATA_MSG_UVDM->BITS.USB_Vendor_ID = SAMSUNG_VENDOR_ID; // VID

	for(i=0;i<6;i++)
		VDO_MSG->VDO[i] = message; // VD01~VDO6 : Max 24bytes

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	// send uVDM message
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = SEL_SSM_MSG_REQ;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	dev_info(&i2c->dev, "%s - message : 0x%x\n", __func__, message);
}

void receive_unstructured_vdm_message(void * data, SSM_MSG_IRQ_STATUS_Type *SSM_MSG_IRQ_State)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_READ;
	uint8_t ReadMSG[32] = {0,};
	u8 W_DATA[1];
	int i;
	
	uint16_t *VID = (uint16_t *)&ReadMSG[6];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&ReadMSG[8];

	s2mm005_read_byte(i2c, REG_ADD, ReadMSG, 32);
	dev_info(&i2c->dev, "%s : VID : 0x%x\n", __func__, *VID);
	for(i=0;i<6;i++)
		dev_info(&i2c->dev, "%s : VDO_%d : %d\n", __func__, i+1, VDO_MSG->VDO[i]);

	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INT_CLEAR;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 1);
}

void send_dna_audio_unstructured_vdm_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_SEND;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[2];
	uint32_t message = (uint32_t)cmd;

	// Message Type Definition
	MSG_HEADER_Type *MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	U_UNSTRUCTURED_VDM_HEADER_Type	*DATA_MSG_UVDM = (U_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[4];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&SendMSG[8];

	// fill message
	MSG_HDR->Message_Type = 15; // send VDM message
	MSG_HDR->Number_of_obj = 7; // VDM Header + 6 VDOs = MAX 7

	DATA_MSG_UVDM->BITS.USB_Vendor_ID = SAMSUNG_VENDOR_ID; // VID

	VDO_MSG->VDO[0] = message;

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	// send uVDM message
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = SEL_SSM_MSG_REQ;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	dev_info(&i2c->dev, "%s - message : 0x%x\n", __func__, message);
}

void send_dex_fan_unstructured_vdm_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_SSM_MSG_SEND;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[2];
	uint32_t message = (uint32_t)cmd;

	// Message Type Definition
	MSG_HEADER_Type *MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	U_UNSTRUCTURED_VDM_HEADER_Type	*DATA_MSG_UVDM = (U_UNSTRUCTURED_VDM_HEADER_Type *)&SendMSG[4];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&SendMSG[8];

	// fill message
	MSG_HDR->Message_Type = 15; // send VDM message
	MSG_HDR->Number_of_obj = 2; // VDM Header + 6 VDOs = MAX 7

	DATA_MSG_UVDM->BITS.USB_Vendor_ID = SAMSUNG_VENDOR_ID; // VID
	DATA_MSG_UVDM->BITS.VENDOR_DEFINED_MESSAGE = 1;

	VDO_MSG->VDO[0] = message;

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	// send uVDM message
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = SEL_SSM_MSG_REQ;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	dev_info(&i2c->dev, "%s - message : 0x%x\n", __func__, message);
}

void send_role_swap_message(void * data, int cmd) // cmd 0 : PR_SWAP, cmd 1 : DR_SWAP
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_I2C_SLV_CMD;
	u8 mode = (u8)cmd;
	u8 W_DATA[2];

	// send uVDM message
	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = mode ? REQ_DR_SWAP : REQ_PR_SWAP;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 2);

	dev_info(&i2c->dev, "%s : sent %s message\n", __func__, mode ? "DR_SWAP" : "PR_SWAP");
}

void send_attention_message(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = REG_TX_DIS_ATTENTION_RESPONSE;
	uint8_t SendMSG[32] = {0,};
	u8 W_DATA[3];
	uint32_t message = (uint32_t)cmd;
	int i;

	// Message Type Definition
	MSG_HEADER_Type *MSG_HDR = (MSG_HEADER_Type *)&SendMSG[0];
	U_DATA_MSG_VDM_HEADER_Type	  *DATA_MSG_VDM = (U_DATA_MSG_VDM_HEADER_Type *)&SendMSG[4];
	VDO_MESSAGE_Type *VDO_MSG = (VDO_MESSAGE_Type *)&SendMSG[8];

	// fill message
	DATA_MSG_VDM->BITS.VDM_command = 6; // attention
	DATA_MSG_VDM->BITS.VDM_Type = 1; // structured VDM
	DATA_MSG_VDM->BITS.Standard_Vendor_ID = SAMSUNG_VENDOR_ID;

	MSG_HDR->Message_Type = 15; // send VDM message
	MSG_HDR->Number_of_obj = 7; // VDM Header + 6 VDOs = MAX 7

	for(i=0;i<6;i++)
		VDO_MSG->VDO[i] = message; // VD01~VDO6 : Max 24bytes

	s2mm005_write_byte(i2c, REG_ADD, SendMSG, 32);

	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = PD_NEXT_STATE;
	W_DATA[2] = 100;
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3);

	dev_info(&i2c->dev, "%s - message : 0x%x\n", __func__, message);
}

void do_alternate_mode_step_by_step(void * data, int cmd)
{
	struct s2mm005_data *usbpd_data = data;
	struct i2c_client *i2c = usbpd_data->i2c;
	uint16_t REG_ADD = 0;
	u8 W_DATA[3];

	REG_ADD = REG_I2C_SLV_CMD;
	W_DATA[0] = MODE_INTERFACE;
	W_DATA[1] = PD_NEXT_STATE;
	switch (cmd) {
		case VDM_DISCOVER_ID:
			W_DATA[2] = 80;
			break;
		case VDM_DISCOVER_SVIDS:
			W_DATA[2] = 83;
			break;
		case VDM_DISCOVER_MODES:
			W_DATA[2] = 86;
			break;
		case VDM_ENTER_MODE:
			W_DATA[2] = 89;
			break;
		case VDM_EXIT_MODE:
			W_DATA[2] = 92;
			break;
	}
	s2mm005_write_byte(i2c, REG_ADD, &W_DATA[0], 3);

	dev_info(&i2c->dev, "%s\n", __func__);
}

#endif
