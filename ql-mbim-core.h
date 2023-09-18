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

#ifndef __QC_MBIM_CORE_
#define __QC_MBIM_CORE_

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <getopt.h>
#include <locale.h>
#include <errno.h>
#include <gio/gio.h>
#include <libmm-glib.h>
#include <libmbim-glib.h>
#include <syslog.h>

enum action
{
    GET_FW_INFO = 0,
    QUERY_SUBSCRIBER_READY_STATUS,
    REBOOT,
    QUERY_DEVICE_CAPS,
    SWITCH_SBL,
    EXEC_AT,
};

#define SET_ACTION(ctx, act)              \
    do                                    \
    {                                     \
        ((ctx)->actions |= (1 << (act))); \
    } while (0)
#define UNSET_ACTION(ctx, act)             \
    do                                     \
    {                                      \
        ((ctx)->actions &= ~(1 << (act))); \
    } while (0)
#define CLEAR_ALL_ACTION(ctx) \
    do                        \
    {                         \
        (ctx)->actions = 0;   \
    } while (0)
#define TEST_ACTION(ctx, act) (!!((ctx)->actions & (1 << (act))))

struct FwUpdaterData
{
    GTask *task;
    GMainLoop *mainloop;
    MbimDevice *mbim_device;

    MMObject *modem;
    MMModem *mm_modem;

    guint32 actions;

    union
    {
        struct
        {
            gchar firmware_info[128];
            guint firmware_info_len;

            gchar carrier_uuid[128];
            guint carrier_uuid_len;
        };

        struct
        {
            gchar at_command[128];
            guint at_command_len;
        };
    };

    const char *subscriber_id;
    const char *sim_iccid;

    int idVendor;
    int idProduct;
    int numInterfaces;
    char cdc_wdm[32];

    int result_code;
    int result_code_set;
};


typedef struct
{
    unsigned int offset;
    unsigned int size;
} offset_size_pair_s;

typedef struct 
{
    unsigned int fw_type;
    offset_size_pair_s version;
    offset_size_pair_s carrier;
} chrome_fw_info_s;

int mbim_reboot_modem(void);
int mbim_prepare_to_flash(void);
int mbim_get_version(char main_version[128],
		char carrier_uuid[128],
		char carrier_version[128],
		char oem_version[128]);

void mbim_device_new_ready(GObject *obj,
			GAsyncResult *res,
			gpointer user_data);
void mbim_device_open_ready(MbimDevice *dev,
			GAsyncResult *res,
			gpointer user_data);

void mbim_switch_sbl_ready(MbimDevice *dev,
			        GAsyncResult *res,
			gpointer user_data);
void mbim_chrome_fw_query_ready(MbimDevice *dev,
				GAsyncResult *res,
				gpointer user_data);
void mbim_exec_at_command_ready(MbimDevice *dev,
				GAsyncResult *res,
				gpointer user_data);
void query_subscriber_ready_status_ready(MbimDevice *device,
                                    GAsyncResult *res,
					gpointer user_data);
void query_device_caps_ready(MbimDevice *device,
                        GAsyncResult *res,
                        gpointer user_data);
void mbim_quec_firmware_update_modem_reboot_set_ready(MbimDevice *dev,
                                                         GAsyncResult *res,
						gpointer user_data);


void mbim_exit(struct FwUpdaterData *ctx);





#endif
