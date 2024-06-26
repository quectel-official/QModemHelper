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

#ifndef __SAHARA_CORE_H__
#define __SAHARA_CORE_H__
#define _GNU_SOURCE
#include <linux/usbdevice_fs.h>
#include <linux/usb/ch9.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
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
#include <errno.h>
#include <fcntl.h>
#include <syslog.h>

#define SWITCHED_TO_EDL 1
#define SWITCHED_TO_SBL 0
#define NORMAL_OPERATION 2

#define NP_VID 0x3731

#define QUEC_SAHARA_FW_UPDATE_PROCESS_REPORT_ID 0x20
#define QUEC_SAHARA_FW_UPDATE_END_ID  0x21

#define QBUFFER_SIZE 4096
#define PATH_LENGTH 512
#define SAHARA_RAW_BUFFER_SIZE (8 * 1024)
#define SINGLE_IMAGE_HDR_SIZE (4 * 1024)

#define MAX_NUM_ENDPOINTS 0xff
#define MAX_NUM_INTERFACES 0xff

#define dbg(fmt, arg...)                                  \
    do                                                    \
    {                                                     \
        char log_buff[512];                               \
        snprintf(log_buff, sizeof(log_buff), fmt, ##arg); \
        printf("%s\n", log_buff);                         \
    } while (0)
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

struct image_layout
{
    uint32_t sequeue;
    uint32_t file_offset; // 4 bytes file offset from image
    uint32_t file_len;
    uint32_t nand_offset;
    uint32_t nand_len;
    uint32_t crc;
}__attribute__ ((__packed__));;


struct single_image_hdr
{
    char magic[4]; //"Quec"
    uint32_t header_crc;
    uint32_t body_crc;
    uint32_t image_size;
    char module_id[32];
    char module_version[64];
    uint32_t is_auth;
    char auth_version[8];
    char reserve[20];

    uint32_t image_num;
    struct image_layout image_list[36];

    char padding[3084];
}__attribute__ ((__packed__));;

typedef enum
{
  QUEC_FW_UPGRADE_SUCCESS = 0,
  QUEC_FW_UPGRADE_INVALID_PRM,
  QUEC_FW_UPGRADE_INVALID_HEADER,
  QUEC_FW_UPGRADE_INVALID_MODULE_VERSION,
  QUEC_FW_UPGRADE_ERR_BAD_BLOCK,
  QUEC_FW_UPGRADE_ERR_PROGRAM,
  QUEC_FW_UPGRADE_ERR_NO_ENOUGH,
  QUEC_FW_UPGRADE_ERR_FLASH_FAILED,
} quec_x_fw_upgrade_err_code;

struct qdl_device
{
    int fd;
    int in_ep;
    int out_ep;
    size_t in_maxpktsize;
    size_t out_maxpktsize;
};

struct sahara_pkt
{
    uint32_t cmd;
    uint32_t length;

    union
    {
        struct
        {
            uint32_t version;
            uint32_t compatible;
            uint32_t max_len;
            uint32_t mode;
        } hello_req;
        struct
        {
            uint32_t version;
            uint32_t compatible;
            uint32_t status;
            uint32_t mode;
            uint32_t reserved0;         // reserved field
            uint32_t reserved1;         // reserved field
            uint32_t reserved2;         // reserved field
            uint32_t reserved3;         // reserved field
            uint32_t reserved4;         // reserved field
            uint32_t reserved5;         // reserved field

        } hello_resp;
        struct
        {
            uint32_t image;
            uint32_t offset;
            uint32_t length;
        } read_req;
        struct
        {
            uint32_t image;
            uint32_t status;
        } eoi;
        struct
        {
        } done_req;
        struct
        {
            uint32_t status;
        } done_resp;
        struct
        {
            uint64_t image;
            uint64_t offset;
            uint64_t length;
        } read64_req;
        struct
        {
            uint32_t image_id; /* ID of image to be transferred */
            uint32_t end_flag; /* offset into image file to read data from */
            uint32_t successful;
        } packet_fw_update_end;
        struct
        {
            uint32_t image_id;    /* ID of image to be transferred */
            uint32_t data_offset; /* offset into image file to read data from */
            uint32_t data_length; /* length of data segment to be retreived */
            uint32_t percent;
        } packet_fw_update_process_report;
    };
};
uint32_t le_uint32(uint32_t v32);
uint8_t to_hex(uint8_t ch);
void print_hex_dump(const char *prefix, const void *buf, size_t len);
int qdl_mode_check();
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len);
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout);
int qdl_open(struct qdl_device *qdl);
int qdl_close(struct qdl_device *qdl);

int sahara_rx_data(struct qdl_device *qdl, void *rx_buffer, size_t bytes_to_read);

int sahara_reboot_modem();
int sahara_flash_carrier(char *file_name);
int sahara_flash_all(char * main_file_path,char*  oem_file_path,char* carrier_file_path);
int flash_mode_check(void);

#endif
