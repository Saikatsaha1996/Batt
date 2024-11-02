
#define BC_CHG_STATUS_GET				0x59
#define BC_CHG_STATUS_SET				0x60
#define MODEL_DEBUG_BOARD	"Debug_Board"

struct battery_charger_set_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			power_state;
	u32			low_capacity;
	u32			high_capacity;
};

struct battery_charger_notify_msg {
	struct pmic_glink_hdr	hdr;
	u32			notification;
};

struct battery_charger_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			battery_id;
	u32			property_id;
	u32			value;
};

struct battery_charger_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	u32			value;
	u32			ret_code;
};

struct battery_model_resp_msg {
	struct pmic_glink_hdr	hdr;
	u32			property_id;
	char			model[MAX_STR_LEN];
};

struct wireless_fw_check_req {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
	u32			fw_size;
};

struct wireless_fw_check_resp {
	struct pmic_glink_hdr	hdr;
	u32			ret_code;
};

struct wireless_fw_push_buf_req {
	struct pmic_glink_hdr	hdr;
	u8			buf[WLS_FW_BUF_SIZE];
	u32			fw_chunk_id;
};

struct wireless_fw_push_buf_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_status;
};

struct wireless_fw_update_status {
	struct pmic_glink_hdr	hdr;
	u32			fw_update_done;
};

struct wireless_fw_get_version_req {
	struct pmic_glink_hdr	hdr;
};

struct wireless_fw_get_version_resp {
	struct pmic_glink_hdr	hdr;
	u32			fw_version;
};

struct battery_charger_ship_mode_req_msg {
	struct pmic_glink_hdr	hdr;
	u32			ship_mode_type;
};

struct psy_state {
	struct power_supply	*psy;
	char			*model;
	const int		*map;
	u32			*prop;
	u32			prop_count;
	u32			opcode_get;
	u32			opcode_set;
};


static void handle_notification(struct battery_chg_dev *bcdev, void *data,
                                size_t len)
{
        struct battery_charger_notify_msg *notify_msg = data;
        struct psy_state *pst = NULL;
#ifdef OPLUS_FEATURE_CHG_BASIC
        int chg_type;
        int sub_chg_type;
#endif

        if (len != sizeof(*notify_msg)) {
                pr_err("Incorrect response length %zu\n", len);
                return;
        }

        pr_debug("notification: %#x\n", notify_msg->notification);

        switch (notify_msg->notification) {
        case BC_BATTERY_STATUS_GET:
        case BC_GENERIC_NOTIFY:
                pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
                break;
        case BC_USB_STATUS_GET:
                pst = &bcdev->psy_list[PSY_TYPE_USB];
                schedule_work(&bcdev->usb_type_work);
                break;
        case BC_WLS_STATUS_GET:
                pst = &bcdev->psy_list[PSY_TYPE_WLS];
                break;
#ifdef OPLUS_FEATURE_CHG_BASIC
        case BC_USB_PLUGIN_IN_EVENT:
                handle_fastchg_usb(1);
                bcdev->usb_online = true;
                break;
        case BC_USB_PLUGIN_OUT_EVENT:
                handle_fastchg_usb(0);
                bcdev->usb_online = false;
                break;
        case BC_PD_SVOOC:
                if ((g_oplus_chip && g_oplus_chip->wireless_support == false)
                        || oplus_get_wired_chg_present() == true) {
                        printk(KERN_ERR "!!!:%s, should set pd_svooc\n", __func__);
                        oplus_usb_set_none_role();
                        bcdev->pd_svooc = true;
                }
                printk(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, bcdev->pd_svooc);
                break;
        case BC_VOOC_STATUS_GET:
                schedule_delayed_work(&bcdev->adsp_voocphy_status_work, 0);
                break;
        case BC_OTG_ENABLE:
                printk(KERN_ERR "!!!!!enable otg\n");
                pst = &bcdev->psy_list[PSY_TYPE_USB];
                bcdev->otg_online = true;
                bcdev->pd_svooc = false;
                schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
                break;
        case BC_OTG_DISABLE:
                printk(KERN_ERR "!!!!!disable otg\n");
                pst = &bcdev->psy_list[PSY_TYPE_USB];
                bcdev->otg_online = false;
                schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
                break;
        case BC_VOOC_VBUS_ADC_ENABLE:
                printk(KERN_ERR "!!!!!vooc_vbus_adc_enable\n");
                bcdev->adsp_voocphy_err_check = true;
                cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
                schedule_delayed_work(&bcdev->adsp_voocphy_err_work, msecs_to_jiffies(8500));
                if (is_ext_chg_ops()) {
                        oplus_chg_disable_charge();
                        oplus_chg_suspend_charger();/*excute in glink loop for real time*/
                } else {
                        schedule_delayed_work(&bcdev->vbus_adc_enable_work, 0);/*excute in work to avoid glink dead loop*/
                }
                break;
        case BC_CID_DETECT:
                printk(KERN_ERR "!!!!!cid detect || no detect\n");
                schedule_delayed_work(&bcdev->cid_status_change_work, 0);
                break;
        case BC_QC_DETECT:
                chg_type = opchg_get_charger_type();
                sub_chg_type = oplus_chg_get_charger_subtype();
                bcdev->real_chg_type = chg_type | (sub_chg_type << 8);
                bcdev->hvdcp_detect_ok = true;
                break;
        case BC_TYPEC_STATE_CHANGE:
                schedule_delayed_work(&bcdev->typec_state_change_work, 0);
                break;
        case BC_PD_SOFT_RESET:
                printk(KERN_ERR "!!!!!PD hard reset happend\n");
                break;
        case BC_CHG_STATUS_GET:
                schedule_delayed_work(&bcdev->chg_status_send_work, 0);
                break;
        case BC_CHG_STATUS_SET:
                schedule_delayed_work(&bcdev->unsuspend_usb_work, 0);
                break;
#endif
        default:
                break;
        }

        if (pst && pst->psy) {
                /*
                 * For charger mode, keep the device awake at least for 50 ms
                 * so that device won't enter suspend when a non-SDP charger
                 * is removed. This would allow the userspace process like
                 * "charger" to be able to read power supply uevents to take
                 * appropriate actions (e.g. shutting down when the charger is
                 * unplugged).
                 */
                power_supply_changed(pst->psy);
                pm_wakeup_dev_event(bcdev->dev, 50, true);
        }
}
