/*    
    Copyright 2023 Quectel Wireless Solutions Co.,Ltd

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "ql-mbim-core.h"
#include "ql-sahara-core.h"

#define VALIDATE_UNKNOWN(str) (str ? str : "unknown")

struct FwUpdaterData s_ctx;

static int find_quectel_mbim_device(struct FwUpdaterData *ctx);
static int log_printf(int lvl, const char *log_msg);
static int log_printf(int lvl, const char *log_msg)
{
    (void)lvl;
     syslog(lvl,"\t%s", log_msg);
     return 0;
     //return printf("\t%s", log_msg);
}
#define error_printf(fmt, arg...)                         \
    do                                                    \
    {                                                     \
        char log_buff[512];                               \
        snprintf(log_buff, sizeof(log_buff), fmt, ##arg); \
        log_printf(1, log_buff);                          \
    } while (0)

#define info_printf(fmt, arg...)                          \
    do                                                    \
    {                                                     \
        char log_buff[512];                               \
        snprintf(log_buff, sizeof(log_buff), fmt, ##arg); \
        log_printf(0, log_buff);                          \
    } while (0)

static int file_get_value(const char *fpath, int base);
int flash_mode_check(void);

int flash_mode_check(void)
{
    struct dirent *ent = NULL;
    DIR *pDir;
    const char *rootdir = "/sys/bus/usb/devices";
    int find = NORMAL_OPERATION;
    int idVendor;
    int numInterfaces;

    pDir = opendir(rootdir);
    if (!pDir)
    {
        info_printf("not open [/sys/bus/usb/devices] dir");
        return 0;
    }

    while ((ent = readdir(pDir)) != NULL)
    {
        int mbim_intf = 0; /* mbim fixed 0 interface*/
        char path[512] = {'\0'};

        snprintf(path, sizeof(path), "%s/%s/idVendor", rootdir, ent->d_name);
        idVendor = file_get_value(path, 16);
        
          
        if (idVendor == 0x05c6) {
            find = SWITCHED_TO_EDL;
            break;
        }
    

        if (idVendor != 0x2c7c)
            continue;

        snprintf(path, sizeof(path), "%s/%s/bNumInterfaces", rootdir, ent->d_name);
        numInterfaces = file_get_value(path, 10);

        if (numInterfaces == 4)
        {
            int bInterfaceClass;
            int bInterfaceProtocol;
            int bInterfaceSubClass;
            int bNumEndpoints;

            snprintf(path, sizeof(path), "%s/%s:1.%d/bInterfaceClass", rootdir, ent->d_name, mbim_intf);
            bInterfaceClass = file_get_value(path, 16);

            snprintf(path, sizeof(path), "%s/%s:1.%d/bInterfaceProtocol", rootdir, ent->d_name, mbim_intf);
            bInterfaceProtocol = file_get_value(path, 16);

            snprintf(path, sizeof(path), "%s/%s:1.%d/bInterfaceSubClass", rootdir, ent->d_name, mbim_intf);
            bInterfaceSubClass = file_get_value(path, 16);

            snprintf(path, sizeof(path), "%s/%s:1.%d/bNumEndpoints", rootdir, ent->d_name, mbim_intf);
            bNumEndpoints = file_get_value(path, 16);

            if (bInterfaceClass == 0x02 && bInterfaceProtocol == 0x00 && bInterfaceSubClass == 0x0e) // mbim interface
            {
                if (bNumEndpoints == 0)
                {
                    find = SWITCHED_TO_SBL;
                }
            }
        }
    }
    closedir(pDir);

    return find;
}



static int file_get_value(const char *fpath, int base)
{
    int value = -1;
    FILE *fp = fopen(fpath, "r");

    if (fp)
    {
        if (fscanf(fp, base == 16 ? "%x" : "%d", &value))
        {
        };
        fclose(fp);
    }

    return value;
}

void mbim_quec_firmware_update_modem_reboot_set_ready(MbimDevice *dev,
                                                  GAsyncResult *res,
                                                  gpointer user_data)
{
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    struct FwUpdaterData *ctx = (struct FwUpdaterData *)user_data;

    UNSET_ACTION(ctx, REBOOT);

    response = mbim_device_command_finish(dev, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        error_printf("%s error: operation failed: %s\n", __func__, error->message);
    }

    mbim_exit(ctx);
}


void query_device_caps_ready(MbimDevice *device,
                             GAsyncResult *res,
                             gpointer user_data)
{
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    MbimDeviceType device_type;
    MbimVoiceClass voice_class;
    MbimCellularClass cellular_class;
    g_autofree gchar *cellular_class_str = NULL;
    MbimSimClass sim_class;
    g_autofree gchar *sim_class_str = NULL;
    MbimDataClass data_class;
    g_autofree gchar *data_class_str = NULL;
    MbimSmsCaps sms_caps;
    g_autofree gchar *sms_caps_str = NULL;
    MbimCtrlCaps ctrl_caps;
    g_autofree gchar *ctrl_caps_str = NULL;
    guint32 max_sessions;
    g_autofree gchar *custom_data_class = NULL;
    g_autofree gchar *device_id = NULL;
    g_autofree gchar *firmware_info = NULL;
    g_autofree gchar *hardware_info = NULL;
    struct FwUpdaterData *ctx = (struct FwUpdaterData *)user_data;

    UNSET_ACTION(ctx, QUERY_DEVICE_CAPS);

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        info_printf("error: operation failed: %s\n", error->message);
        mbim_exit(ctx);
        return;
    }

    if (!mbim_message_device_caps_response_parse(
            response,
            &device_type,
            &cellular_class,
            &voice_class,
            &sim_class,
            &data_class,
            &sms_caps,
            &ctrl_caps,
            &max_sessions,
            &custom_data_class,
            &device_id,
            &firmware_info,
            &hardware_info,
            &error))
    {
        info_printf("error: couldn't parse response message: %s\n", error->message);
        mbim_exit(ctx);
        return;
    }


    cellular_class_str = mbim_cellular_class_build_string_from_mask(cellular_class);
    sim_class_str = mbim_sim_class_build_string_from_mask(sim_class);
    data_class_str = mbim_data_class_build_string_from_mask(data_class);
    sms_caps_str = mbim_sms_caps_build_string_from_mask(sms_caps);
    ctrl_caps_str = mbim_ctrl_caps_build_string_from_mask(ctrl_caps);

    info_printf("Device capabilities retrieved:\n"
                "\t        Device ID: '%s'\n"
                "\t    Firmware info: '%s'\n"
                "\t    Hardware info: '%s'\n",
                VALIDATE_UNKNOWN(device_id),
                VALIDATE_UNKNOWN(firmware_info),
                VALIDATE_UNKNOWN(hardware_info));

    if (firmware_info)
    {
        memcpy((void *)ctx->firmware_info, (void *)firmware_info, MIN(sizeof(ctx->firmware_info), strlen(firmware_info)));
    }

    mbim_exit(ctx);
}


void query_subscriber_ready_status_ready(MbimDevice *device,
                                         GAsyncResult *res,
                                         gpointer user_data)
{
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    MbimSubscriberReadyState ready_state;
    const gchar *ready_state_str;
    g_autofree gchar *subscriber_id = NULL;
    g_autofree gchar *sim_iccid = NULL;
    MbimReadyInfoFlag ready_info;
    guint32 telephone_numbers_count;
    g_auto(GStrv) telephone_numbers = NULL;
    MbimSubscriberReadyStatusFlag flags;

    struct FwUpdaterData *ctx = (struct FwUpdaterData *)user_data;

    UNSET_ACTION(ctx, QUERY_SUBSCRIBER_READY_STATUS);

    response = mbim_device_command_finish(device, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        info_printf("error: operation failed: %s\n", error->message);
        mbim_exit(ctx);
        return;
    }

    /* MBIMEx 3.0 support */
    if (mbim_device_check_ms_mbimex_version(device, 3, 0))
    {
        if (!mbim_message_ms_basic_connect_v3_subscriber_ready_status_response_parse(response,
                                                                                     &ready_state,
                                                                                     &flags,
                                                                                     &subscriber_id,
                                                                                     &sim_iccid,
                                                                                     &ready_info,
                                                                                     &telephone_numbers_count,
                                                                                     &telephone_numbers,
                                                                                     &error))
        {

            g_printerr("error: couldn't parse response message: %s\n", error->message);
            mbim_exit(ctx);
            return;
        }
        info_printf("Successfully parsed response as MBIMEx 3.0 Subscriber State\n");
    }

    /* MBIM 1.0 support */
    else
    {
        if (!mbim_message_subscriber_ready_status_response_parse(response,
                                                                 &ready_state,
                                                                 &subscriber_id,
                                                                 &sim_iccid,
                                                                 &ready_info,
                                                                 &telephone_numbers_count,
                                                                 &telephone_numbers,
                                                                 &error))
        {

            info_printf("error: couldn't parse response message: %s\n", error->message);
            mbim_exit(ctx);
            return;
        }
        info_printf("Successfully parsed response as MBIM 1.0 Subscriber State\n");
    }

    ready_state_str = mbim_subscriber_ready_state_get_string(ready_state);
    info_printf(" Subscriber ready status retrieved:\n"
                "\t      Ready state: '%s'\n"
                "\t    Subscriber ID: '%s'\n"
                "\t        SIM ICCID: '%s'\n",
                VALIDATE_UNKNOWN(ready_state_str),
                VALIDATE_UNKNOWN(subscriber_id),
                VALIDATE_UNKNOWN(sim_iccid));

    if (subscriber_id)
        ctx->subscriber_id = strdup(subscriber_id);
    if (sim_iccid)
        ctx->sim_iccid = strdup(sim_iccid);

    mbim_exit(ctx);
}


void mbim_exit(struct FwUpdaterData *ctx)
{
    if (ctx->actions == 0)
    {
        g_main_loop_quit(ctx->mainloop);
    }
}


void mbim_exec_at_command_ready(MbimDevice *dev,
                                GAsyncResult *res,
                                gpointer user_data)
{
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    struct FwUpdaterData *ctx = (struct FwUpdaterData *)user_data;

    const guint8 *at_resp = NULL;
    guint32 at_resp_len;

    UNSET_ACTION(ctx, EXEC_AT);

    response = mbim_device_command_finish(dev, res, &error);

    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        error_printf("%s error: operation failed: %s\n", __func__, error->message);
    }
    else
    {
        at_resp = mbim_message_command_done_get_raw_information_buffer(response, &at_resp_len);

        printf("<\n%s", at_resp + 4);
    }

    mbim_exit(ctx);
}

void mbim_chrome_fw_query_ready(MbimDevice *dev,
                                GAsyncResult *res,
                                gpointer user_data)
{

    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    struct FwUpdaterData *ctx = (struct FwUpdaterData *)user_data;

    const chrome_fw_info_s *fw_info = NULL;
    guint32 fw_info_len = 0;
    unsigned int i = 0;

    UNSET_ACTION(ctx, GET_FW_INFO);
    response = mbim_device_command_finish(dev, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
        error_printf("%s error: operation failed: %s\n", __func__, error->message);
    }
    else
    {
        fw_info = (chrome_fw_info_s *)mbim_message_command_done_get_raw_information_buffer(response, &fw_info_len);
         /*version string: utf16 ----> utf8*/
         memset(ctx->firmware_info, 0, sizeof(ctx->firmware_info));
        for (i = 0; i * 2 < fw_info->version.size; i++ )
            ctx->firmware_info[i] = ((char *)fw_info)[fw_info->version.offset +  i * 2];
        ctx->firmware_info_len = i;
        /* carrier_uuid string: utf16 ----> utf8 */
        memset(ctx->carrier_uuid, 0, sizeof(ctx->carrier_uuid));
        ctx->carrier_uuid_len = 0;
        /* This memory section will not exist in the older versions of firmware */
        if ( fw_info->carrier.size < 128)
        {
          for (i = 0; i * 2 < fw_info->carrier.size; i++ )
          {
            ctx->carrier_uuid[i] = ((char *)fw_info)[fw_info->carrier.offset +  i * 2];
          }
          ctx->carrier_uuid_len = i;
        }
    }
    mbim_exit(ctx);
}


void mbim_switch_sbl_ready(MbimDevice *dev,
                           GAsyncResult *res,
                           gpointer user_data)
{
    g_autoptr(MbimMessage) response = NULL;
    g_autoptr(GError) error = NULL;
    struct FwUpdaterData *ctx = (struct FwUpdaterData *)user_data;

    UNSET_ACTION(ctx, SWITCH_SBL);

    response = mbim_device_command_finish(dev, res, &error);
    if (!response || !mbim_message_response_get_result(response, MBIM_MESSAGE_TYPE_COMMAND_DONE, &error))
    {
    }

    mbim_exit(ctx);
}



void mbim_device_open_ready(MbimDevice *dev,
                            GAsyncResult *res,
                            gpointer user_data)
{
    g_autoptr(GError) error = NULL;
    struct FwUpdaterData *ctx = (struct FwUpdaterData *)(user_data);

    if (!mbim_device_open_finish(dev, res, &error))
    {
        error_printf("error: couldn't open the MbimDevice: %s\n",
                     error->message);
        g_main_loop_quit(ctx->mainloop);
    }

    if (TEST_ACTION(ctx, SWITCH_SBL))
    {
        guint8 data[2] = {};

        g_autoptr(MbimMessage) request = mbim_message_command_new(0, MBIM_SERVICE_QDU, 0x0d, MBIM_MESSAGE_COMMAND_TYPE_SET);
        mbim_message_command_append(request, data, 2);

        mbim_device_command(ctx->mbim_device,
                            request,
                            1 /*this mbim commond have no resp,just waiting for 1S*/,
                            NULL,
                            (GAsyncReadyCallback)mbim_switch_sbl_ready,
                            ctx);
    }

    if (TEST_ACTION(ctx, GET_FW_INFO))
    {
        guint8 data[2] = {};

        g_autoptr(MbimMessage) request = mbim_message_command_new(0, MBIM_SERVICE_QDU, 0x0e, MBIM_MESSAGE_COMMAND_TYPE_SET);
        mbim_message_command_append(request, data, 2);

        mbim_device_command(ctx->mbim_device,
                            request,
                            10,
                            NULL,
                            (GAsyncReadyCallback)mbim_chrome_fw_query_ready,
                            ctx);
    }

    if (TEST_ACTION(ctx, EXEC_AT))
    {
        guint8 data[128] = {};

        g_autoptr(MbimMessage) request = mbim_message_command_new(0, MBIM_SERVICE_QDU, 0x08, MBIM_MESSAGE_COMMAND_TYPE_SET);

        if ((ctx->at_command_len + 4) > sizeof(data))
            return;

        strncpy((char *)data + 4, ctx->at_command, ctx->at_command_len);
        mbim_message_command_append(request, data, ctx->at_command_len + 4);

        printf("> %s\n", ctx->at_command);

        mbim_device_command(ctx->mbim_device,
                            request,
                            10,
                            NULL,
                            (GAsyncReadyCallback)mbim_exec_at_command_ready,
                            ctx);
    }

    if (TEST_ACTION(ctx, QUERY_SUBSCRIBER_READY_STATUS))
    {
        g_autoptr(MbimMessage) request = (mbim_message_subscriber_ready_status_query_new(NULL));
        mbim_device_command(ctx->mbim_device,
                            request,
                            10,
                            NULL,
                            (GAsyncReadyCallback)query_subscriber_ready_status_ready,
                            ctx);
    }

    if (TEST_ACTION(ctx, QUERY_DEVICE_CAPS))
    {
        g_autoptr(MbimMessage) request = (mbim_message_device_caps_query_new(NULL));
        mbim_device_command(ctx->mbim_device,
                            request,
                            10,
                            NULL,
                            (GAsyncReadyCallback)query_device_caps_ready,
                            ctx);
    }

    if (TEST_ACTION(ctx, REBOOT))
    {
        g_autoptr(MbimMessage) request = mbim_message_intel_firmware_update_modem_reboot_set_new(NULL);
        mbim_device_command(ctx->mbim_device,
                            request,
                            10,
                            NULL,
			(GAsyncReadyCallback)mbim_quec_firmware_update_modem_reboot_set_ready,
                            ctx);
    }
}



void mbim_device_new_ready(GObject *obj,
                           GAsyncResult *res,
                           gpointer user_data)
{
    GError *error = NULL;

    struct FwUpdaterData *ctx = (struct FwUpdaterData *)(user_data);

    info_printf("\n%s %s %d\n",__FILE__,__FUNCTION__,__LINE__);

    ctx->mbim_device = mbim_device_new_finish(res, &error);
    if (!ctx->mbim_device)
    {
        g_printerr("error: couldn't create MbimDevice: %s\n",
                   error->message);
        g_main_loop_quit(ctx->mainloop);
        return;
    }

    /* Open the device */
    mbim_device_open_full(ctx->mbim_device,
                          MBIM_DEVICE_OPEN_FLAGS_PROXY,
                          30,
                          NULL,
                          (GAsyncReadyCallback)mbim_device_open_ready,
                          user_data);
}



static int find_quectel_mbim_device(struct FwUpdaterData *ctx)
{
    struct dirent *ent = NULL;
    DIR *pDir;
    const char *rootdir = "/sys/bus/usb/devices";
    int find = 0;

    pDir = opendir(rootdir);
    if (!pDir)
    {
        info_printf("could not open [/sys/bus/usb/devices] dir");
        return 0;
    }

    while ((ent = readdir(pDir)) != NULL)
    {
        char path[512] = {'\0'};
        int mbim_intf = 0; /* mbim fixed 0 interface*/
        int i = 0;

        snprintf(path, sizeof(path), "%s/%s/idVendor", rootdir, ent->d_name);
        ctx->idVendor = file_get_value(path, 16);
        if (ctx->idVendor != 0x2c7c && ctx->idVendor != 0x05c6)
            continue;

        snprintf(path, sizeof(path), "%s/%s/idProduct", rootdir, ent->d_name);
        ctx->idProduct = file_get_value(path, 16);
        snprintf(path, sizeof(path), "%s/%s/bNumInterfaces", rootdir, ent->d_name);
        ctx->numInterfaces = file_get_value(path, 10);

        for (i = 0; i < 15; i++)
        {
            snprintf(path, sizeof(path), "%s/%s:1.%d/usbmisc/cdc-wdm%d", rootdir, ent->d_name, mbim_intf, i);
            if (!access(path, R_OK))
            {
                snprintf(ctx->cdc_wdm, sizeof(ctx->cdc_wdm), "/dev/cdc-wdm%d", i);
                find++;
                break;
            }
        }

        if (find)
        {
            info_printf("%s %x, %x, %d, %s\n", __func__,
                        ctx->idVendor, ctx->idProduct, ctx->numInterfaces, ctx->cdc_wdm);
            break;
        }
    }
    closedir(pDir);

    return find;
}


int mbim_reboot_modem(void)
{
    struct FwUpdaterData *ctx = &s_ctx;
    g_autoptr(GFile) file = NULL;
    if (!find_quectel_mbim_device(ctx)) {
        info_printf("Could not find a Quectel modem available for commands!\n");
        return EXIT_FAILURE;
    }

    SET_ACTION(ctx, REBOOT);
    ctx->mainloop = g_main_loop_new(NULL, FALSE);
    file = g_file_new_for_path(ctx->cdc_wdm);
    mbim_device_new(g_file_new_for_path(ctx->cdc_wdm), NULL, (GAsyncReadyCallback)mbim_device_new_ready, ctx);
    g_main_loop_run(ctx->mainloop);
    g_main_loop_unref(ctx->mainloop);
    return 0;
}


int mbim_prepare_to_flash(void)
{
    int i;

    struct FwUpdaterData *ctx = &s_ctx;
    g_autoptr(GFile) file = NULL;

    if (flash_mode_check() != NORMAL_OPERATION)
    {
        info_printf("Already in download mode\n");
        return 0;
    }

    if (!find_quectel_mbim_device(ctx))
    {
        info_printf("quectel mbim device not found\n");
        return -1;
    }

    info_printf("Switching the device into flashing mode\n");
    SET_ACTION(ctx, SWITCH_SBL);
    ctx->mainloop = g_main_loop_new(NULL, FALSE);
    file = g_file_new_for_path(ctx->cdc_wdm);

    info_printf("Mbim initialization\n");
    mbim_device_new(g_file_new_for_path(ctx->cdc_wdm), NULL, (GAsyncReadyCallback)mbim_device_new_ready, ctx);
    info_printf("Mbim initialization main loop\n");
    g_main_loop_run(ctx->mainloop);
    g_main_loop_unref(ctx->mainloop);

    for (i = 0; i < 10; i++) // wait 5s
    {
        usleep(500000); // 0.5S

        if (flash_mode_check() != NORMAL_OPERATION)
        {
            return 0;
        }
    }

    return -1;
}


int mbim_get_version(char main_version[128],
                     char carrier_uuid[128],
                     char carrier_version[128],
                     char oem_version[128])
{
    struct FwUpdaterData *ctx = &s_ctx;
    g_autoptr(GFile) file = NULL;

    char *p;
    int m1, m2, o1, o2, c1, c2;

    if (!find_quectel_mbim_device(ctx))
    {
        info_printf("quectel mbim device not found\n");
        return -1;
    }

    info_printf("Quectel mbim device found!\n");

    strcpy(carrier_uuid, "generic");
    main_version[0] = 0;
    oem_version[0] = 0;
    carrier_version[0] = 0;

    SET_ACTION(ctx, GET_FW_INFO);

    ctx->mainloop = g_main_loop_new(NULL, FALSE);

    file = g_file_new_for_path(ctx->cdc_wdm);

    info_printf(" %s %d Quectel mbim device initialization!\n",__FILE__,__LINE__);
    mbim_device_new(file, NULL, (GAsyncReadyCallback)mbim_device_new_ready, ctx);

    g_main_loop_run(ctx->mainloop);
    g_main_loop_unref(ctx->mainloop);

    if (!g_strrstr(ctx->firmware_info, "EM060KGL"))
    {

        info_printf("can not get EM060KGL product firmware info from module\n");
        return -1;
    }

    if ((p = strrchr(ctx->firmware_info, '_')) == NULL)
    {
        info_printf("%s: wrong format\n", ctx->firmware_info);
        return -1;
    }

    if (sscanf(p + 1, "%02d.%03d.%02d.%03d.%02d.%03d", &m1, &m2, &o1, &o2, &c1, &c2) != 6)
    {
        info_printf("%s: wrong format\n", ctx->firmware_info);
        return -1;
    }

    sprintf(main_version, "%02d.%03d", m1, m2);
    sprintf(carrier_version, "%02d.%03d", c1, c2);
    sprintf(oem_version, "%02d.%03d", o1, o2);

    if (ctx->carrier_uuid_len > 0)
        strncpy(carrier_uuid, ctx->carrier_uuid, ctx->carrier_uuid_len);

    info_printf("[debug]main_version:%s\n", main_version);
    info_printf("[debug]carrier_version:%s\n", carrier_version);
    info_printf("[debug]oem_version:%s\n", oem_version);
    info_printf("[debug]carrier_uuid:%s\n", carrier_uuid);

    return 0;
}
