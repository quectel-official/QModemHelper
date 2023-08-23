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
#include <errno.h>
#include <stdint.h>
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <fcntl.h>
#include <getopt.h>
#include <libudev.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

static int print_help();
static int parse_flash_fw_parameters(char *arg, char *main_fw, char *oem_fw, char *carrier_fw);

static int print_help(int argc,char *argv[])
{
    printf("\nQuectel modem helper 0.1\n");
        printf("\n=================================\n");
        if (argc < 2)
        {
            fprintf(stderr, "Too few arguments!\n");
        }

        fprintf(stderr,"%s", argv[0]);
        fprintf(stderr,"%s\n", "  --get_fw_info");
        fprintf(stderr,"%s\n", "  --prepare_to_flash");
        fprintf(stderr,"%s\n", "  --flash_fw");
        fprintf(stderr,"%s\n", "  --reboot");
        fprintf(stderr,"%s\n", "  --help");

        return 0;
}


static int parse_flash_fw_parameters(char *arg, char *main_fw, char *oem_fw, char *carrier_fw)
{
    char *str, *segment, *saveptr, *saveptr2;
    char *type, *path;

    for (str = arg;; str = NULL)
    {
        segment = strtok_r(str, ",", &saveptr);
        if (segment == NULL)
            break;

        type = strtok_r(segment, ":", &saveptr2);
        path = strtok_r(NULL, ":", &saveptr2);

        if (type == NULL || path == NULL)
            break;

        if (oem_fw && strcmp(type, "oem") == 0)
        {
            strcpy(oem_fw, path);
            printf("%s : oem section found : %s\n",__FUNCTION__, path);
        }

        if (main_fw && strcmp(type, "main") == 0)
        {
            strcpy(main_fw, path);
            printf("%s : main section found\n",__FUNCTION__);
        }

        if (carrier_fw && strcmp(type, "carrier") == 0)
        {
            strcpy(carrier_fw, path);
            printf("%s : carrier section found\n",__FUNCTION__);
        }
    }
    return 0;
}



int main(int argc, char *argv[])
{
    struct option longopts[] = {
            {"get_fw_info", 0, NULL, 'G'},
            {"prepare_to_flash", 0, NULL, 'P'},
            {"flash_fw", 1, NULL, 'A'},
            {"reboot", 0, NULL, 'R'},
            {"sahara-reboot", 0, NULL, 'S'},
            {"help", 0, NULL, 'H'},
            {},
    };
    int opt;
    int ret;
    char oem_file_path[1024];
    char carrier_file_path[1024];
    char main_file_path[1024];
    memset(oem_file_path , 0 , 1024);
    memset(carrier_file_path , 0 , 1024);
    memset(main_file_path , 0 , 1024);
    while ( -1 != (opt = getopt_long(argc, argv, "h:", longopts, NULL))) 
    {
        switch (opt)
        {
            case 'G':
            {
                char main_version[128] = {};
                char carrier_uuid[128] = {};
                char carrier_version[128] = {};
                char oem_version[128] = {};
                ret = mbim_get_version(main_version, carrier_uuid, carrier_version, oem_version);
                if (ret)
                {
                    printf("%s:%s\n", "main", main_version);
                    printf("%s:%s\n", "carrier_uuid", carrier_uuid);
                    printf("%s:%s\n", "carrier", carrier_version);
                    if (oem_version[0])
                        printf("%s:%s\n", "oem", oem_version);
                }
                break;
            }

            case 'P':
            {
                ret = mbim_prepare_to_flash();
                printf("put the modem into firmware download mode %d\n", ret);
                return ret;
            }
            break;

            case 'A':
                parse_flash_fw_parameters(optarg,
                                          main_file_path,
                                          oem_file_path,
                                          carrier_file_path);
                if ( strlen(main_file_path) && strlen(oem_file_path)  && strlen(carrier_file_path) )
                {
                    ret = sahara_flash_all(main_file_path, oem_file_path, carrier_file_path);
                    printf("sahara_flash_all(): %d\n", ret);
                    return ret;
                }

                if ( strlen(carrier_file_path) )
                {
                    ret =  sahara_flash_carrier(carrier_file_path);
                    printf("sahara_flash_carrier(): %d\n", ret);
                    return ret;
                }

                if ( strlen(main_file_path) )
                {
                    ret = sahara_flash_carrier(main_file_path);
                    printf("sahara_flash_carrier(): %d\n", ret);
                    return ret;
                }

                if ( strlen(oem_file_path) )
                {
                    ret = sahara_flash_carrier(oem_file_path);
                    printf("sahara_flash_carrier(): %d\n", ret);
                    return ret;
                }

                printf("\nNo file name was supplied\n");
                break;
            case 'T':
                printf("carrier path: %s\n", optarg);
                ret = sahara_flash_carrier(optarg);
                printf("sahara_flash_carrier(): %d\n", ret);
                break;

            case 'R':
                ret = mbim_reboot_modem();
                printf("mbim_reboot_modem(): %d\n", ret);
                break;

            case 'S':
                ret = sahara_reboot_modem();
                printf("sahara_reboot_modem(): %d\n", ret);
                break ;

            case 'H':
                print_help(argc, argv);
                break;

            case 'h':
                print_help(argc, argv);
                break;

            default:
                print_help(argc, argv);
                break;
            }
        }

        return 0;
}
