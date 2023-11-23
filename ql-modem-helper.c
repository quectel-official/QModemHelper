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
#include "ql-gpio.h"
#include "ql-qdl-sahara.h"
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

#define MAX_FILE_NAME_LEN 1024

const char kPowerOverrideLockDirectoryPath[] = "/run/lock/power_override";
const char kPowerOverrideLockFileName[] = "qmodemhelper.lock";
//Keys for the parameters that modemfwd will call the helper all these need to handled
// not all the features need to implemented
const char kGetFirmwareInfo[] = "get_fw_info";
const char kShillFirmwareRevision[] = "shill_fw_revision";
const char kPrepareToFlash[] = "prepare_to_flash";
const char kFlashFirmware[] = "flash_fw";
const char kResetGpioLine[] = "power_enable_gpio";
const char kFlashModeCheck[] = "flash_mode_check";
const char kReboot[] = "reboot";
const char kClearAttachAPN[] = "clear_attach_apn";
const char kFwVersion[] = "fw_version";

// Keys used for the kFlashFirmware/kFwVersion/kGetFirmwareInfo switches
const char kFwMain[] = "main";
const char kFwCarrier[] = "carrier";
const char kFwOem[] = "oem";
const char kFwAp[] = "ap";
const char kFwDev[] = "dev";
const char kFwCarrierUuid[] = "carrier_uuid";
const char kUnknownRevision[] = "unknown-revision";

static int print_help(int);
static int parse_flash_fw_parameters(char *arg, char *main_fw, char *oem_fw, char *carrier_fw);
static int power_lock( const char* path, const char* filename);
static int power_unlock(const char* path, const char* filename);

static int power_lock(const char* path, const char* filename)
{
	char* full_file_path = NULL;
	FILE *lock_file;

	if  (!(path && filename)) {
		return EXIT_FAILURE;
	}
	full_file_path = (char *)malloc(strlen(path)+strlen(filename)+2);
	memset(full_file_path, 0 , strlen(path)+strlen(filename)+2);
	strncat(full_file_path,path, strlen(path));
	strncat(full_file_path,"/",1);
	strncat(full_file_path,filename, strlen(filename));

  syslog(0, "Creating lock file %s \n", full_file_path);
	if ((lock_file = fopen(full_file_path,"w")) == NULL) {
			return EXIT_FAILURE;
	}

	fprintf(lock_file, "%d", getpid());
	fclose(lock_file);
	return EXIT_SUCCESS;
}

static int power_unlock(const char* path, const char* filename)
{
	char* full_file_path = NULL;

	if  (!(path && filename)) {
		return EXIT_FAILURE;
	}

	full_file_path = (char *)malloc(strlen(path)+strlen(filename)+2);
	memset(full_file_path, 0 , strlen(path)+strlen(filename)+2);
	strncat(full_file_path,path, strlen(path));
	strncat(full_file_path,"/",1);
	strncat(full_file_path,filename, strlen(filename));

	if (remove(full_file_path) != 0) {
		    syslog(0, "Unable to remove the lock file\n");
				return EXIT_FAILURE;
	}
    syslog(0, "Lockfile removed\n");
	return EXIT_SUCCESS;
}

static int print_help(int argc)
{
    printf("\nQuectel modem helper 0.1\n");
    printf("\n=================================\n");
    if (argc < 2) {
        fprintf(stderr, "Too few arguments!\n");
    }

    fprintf(stderr,"   --%s\n", kGetFirmwareInfo);
    fprintf(stderr,"   --%s\n", kPrepareToFlash);
    fprintf(stderr,"   --%s\n", kFlashFirmware);
	fprintf(stderr,"   --%s\n", kResetGpioLine);
	fprintf(stderr,"   --%s\n", kFlashModeCheck);
    fprintf(stderr,"   --%s\n", kReboot);
    fprintf(stderr,"   --help\n");
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

        if (oem_fw && strcmp(type, kFwOem) == 0)
        {
            strcpy(oem_fw, path);
	    strcat(oem_fw,"/oem.bin");
            syslog(0, "%s : oem section found : %s\n",__FUNCTION__, path);
        }

        if (main_fw && strcmp(type, kFwMain) == 0)
        {
            strcpy(main_fw, path);
	    strcat(main_fw,"/main.bin");
            syslog(0, "%s : main section found: %s\n",__FUNCTION__, path);
        }

        if (carrier_fw && strcmp(type, kFwCarrier) == 0)
        {
            strcpy(carrier_fw, path);
	    strcat(carrier_fw,"/carrier.bin");
            syslog(0, "%s : carrier section found: %s\n",__FUNCTION__, path);
        }
    }
    return 0;
}

void get_version()
{
	int ret;
	char main_version[128] = {};
	char carrier_uuid[128] = {};
	char carrier_version[128] = {};
	char oem_version[128] = {};
	ret = mbim_get_version(main_version, carrier_uuid, carrier_version, oem_version);
	if (ret == 0)
	{
		printf("%s:%s\n", kFwMain, main_version);
		printf("%s:%s\n", kFwCarrierUuid, carrier_uuid);
		printf("%s:%s\n", kFwCarrier, carrier_version);
		if (strlen(oem_version)) {
                        printf("%s:%s\n", kFwOem, oem_version);
		} else {
			printf("%s:%s\n", kFwOem, kUnknownRevision);
		}
	}
	closelog();
}

int flash_firmware(char *arg)
{
	int ret;
	char oem_file_path[MAX_FILE_NAME_LEN];
	char carrier_file_path[MAX_FILE_NAME_LEN];
	char main_file_path[MAX_FILE_NAME_LEN];
	memset(oem_file_path , 0 , MAX_FILE_NAME_LEN);
	memset(carrier_file_path , 0 , MAX_FILE_NAME_LEN);
	memset(main_file_path , 0 , MAX_FILE_NAME_LEN);

  parse_flash_fw_parameters(arg,
                            main_file_path,
                            oem_file_path,
                            carrier_file_path);


  if (qdl_mode_check() ==  SWITCHED_TO_EDL) {
    // Modem is in qdl mode. sahara_flash_all will handle it.
    syslog(0, "The device is switched to EDL mode. \n");
    ret = qdl_flash_all(main_file_path, oem_file_path, carrier_file_path);
    if (ret) {
      return EXIT_FAILURE;
    }
    closelog();
    return 0;
  }

	if (mbim_prepare_to_flash()) {
    return EXIT_FAILURE;
	}
	ret = sahara_flash_all(main_file_path, oem_file_path, carrier_file_path);
	if (ret != 0)
		return EXIT_FAILURE;
	closelog();
	return 0;
}

int main(int argc, char *argv[])
{
    struct option longopts[] = {
        {kGetFirmwareInfo, 0, NULL, 'G'},
        {kPrepareToFlash, 0, NULL, 'P'},
        {kFlashFirmware, 1, NULL, 'A'},
        {kFlashModeCheck, 0, NULL, 'M'},
		    {kResetGpioLine, 2, NULL, 'N'},
        {kReboot, 0, NULL, 'R'},
        {"help", 0, NULL, 'H'},
        {},
    };
    uint reset_line = 0;
    int opt;
    int ret;
    int reset_flag = 0;

    openlog ("qmodemhelper", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);

    for (int i=1; i<argc;i++) {
      syslog(0,"\t Argument %d : %s",i, argv[i]);
    }


    while ( -1 != (opt = getopt_long(argc, argv, "h:", longopts, NULL)))
    {
        switch (opt)
        {
            case 'G':
				get_version();
                return 0;
            case 'P':
				if (mbim_prepare_to_flash()) {
		       		return EXIT_FAILURE;
				}
                syslog(0, "Swithing the modem into firmware download mode %d\n", ret);
				return 0;
            case 'A':
				if (power_lock(kPowerOverrideLockDirectoryPath, kPowerOverrideLockFileName) !=0) {
					printf("Cannot aquire file lock\n");
					return EXIT_FAILURE;
				}
				ret = flash_firmware(optarg);
				power_unlock(kPowerOverrideLockDirectoryPath, kPowerOverrideLockFileName);
				return ret;
            case 'R':
							  reset_flag = 1;
                break;
            case 'M':
				if (flash_mode_check() != NORMAL_OPERATION) {
					printf("true\n");
				} else {
					printf("false\n");
				}
                return 0;
			case 'N':
				reset_line = atoi(optarg);
				printf("Baseline offset %d\n", reset_line);
				break;
            case 'H':
                print_help(argc);
                return 0;
            case 'h':
                print_help(argc);
                return 0;
            default:
                break;
            }
        }

		if ((reset_flag) && (reset_line)) {
			if (power_lock(kPowerOverrideLockDirectoryPath, kPowerOverrideLockFileName) !=0) {
				printf("Cannot aquire file lock\n");
				return EXIT_FAILURE;
			}
			printf("Reseting line: %d\n", reset_line );
			ret = gpio_reboot_modem(reset_line);
			if (ret) {
				printf("Failed to reset line: %d\n", reset_line );
			}
			else {
				printf("Modem is rebooting\n");
			}
			power_unlock(kPowerOverrideLockDirectoryPath, kPowerOverrideLockFileName);
			return ret;
			}
        closelog();
        return EXIT_FAILURE;
}
