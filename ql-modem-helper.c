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
#include <gpiod.h>

#define MAX_FILE_NAME_LEN 1024
#define HELPERID "GPIO_HELPER"
#define RESET_LINE "LTE_RESET_L"
#define GPIO_CHIP_LOCATION "/dev/gpiochip0"

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
static int gpio_reboot_modem();

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


    // Check if file exists
    if (access(full_file_path, F_OK) == 0) {
			printf("Lock file already exists \n");
    	return EXIT_FAILURE;
    }

    printf("Creating lock file %s \n", full_file_path);
	if ((lock_file = fopen(full_file_path,"a")) == NULL) {
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
		    printf("Unable to remove the lock file\n");
				return EXIT_FAILURE;
	}
    printf("Lockfile removed\n");
	return EXIT_SUCCESS;
}

static int gpio_reboot_modem(uint reset_line)
{
  struct gpiod_chip *chip;
	struct gpiod_line *line;
	int req;
  chip = gpiod_chip_open(GPIO_CHIP_LOCATION);
  if (!chip) {
	  printf("\n Can't open %s", GPIO_CHIP_LOCATION);
	  return EXIT_FAILURE;
  }

  line = gpiod_chip_get_line(chip, reset_line);
  if (!line) {
	  printf("\n Can't open the line: %d\n", reset_line);
	  gpiod_chip_close(chip);
	  return EXIT_FAILURE;
  }

  req = gpiod_line_request_output(line, HELPERID, 0);
  if (req) {
	  printf("\n Can't set the line for output: %d\n", reset_line);
	  gpiod_chip_close(chip);
	  return EXIT_FAILURE;
  }

  req = gpiod_line_set_value(line, 0);

  if (req) {
	  printf("\n Can't set the line %d to low\n", reset_line);
	  gpiod_chip_close(chip);
	  return EXIT_FAILURE;
  }

  req = gpiod_line_set_value(line, 1);
  if (req) {
	  printf("\n Can't set the line %d to high\n", reset_line);
	  gpiod_chip_close(chip);
	  return EXIT_FAILURE;
  }

  gpiod_line_release(line);
  gpiod_chip_close(chip);
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
            printf("%s : oem section found : %s\n",__FUNCTION__, path);
        }

        if (main_fw && strcmp(type, kFwMain) == 0)
        {
            strcpy(main_fw, path);
	    strcat(main_fw,"/main.bin");
            printf("%s : main section found: %s\n",__FUNCTION__, path);
        }

        if (carrier_fw && strcmp(type, kFwCarrier) == 0)
        {
            strcpy(carrier_fw, path);
	    strcat(carrier_fw,"/carrier.bin");
            printf("%s : carrier section found: %s\n",__FUNCTION__, path);
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

	ret = mbim_prepare_to_flash();
	if (ret != 0)
		return EXIT_FAILURE;
	parse_flash_fw_parameters(arg,
				main_file_path,
				oem_file_path,
				carrier_file_path);
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
                printf("Swithing the modem into firmware download mode %d\n", ret);
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
				if (flash_mode_check()) {
					printf("\nModem is in flashing mode\n");
				} else {
					printf("\nModem is in normal operating mode\n");
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
			if (gpio_reboot_modem(reset_line)) {
				printf("Failed to reset line: %d\n", reset_line );
			}
			power_unlock(kPowerOverrideLockDirectoryPath, kPowerOverrideLockFileName);
			printf("Modem is rebooting\n");
			}
        closelog();
        return EXIT_FAILURE;
}
