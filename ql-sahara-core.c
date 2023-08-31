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

#include "ql-sahara-core.h"


#define dbg_time printf
bool qdl_debug;


static uint32_t le_uint32(uint32_t v32);
static uint8_t to_hex(uint8_t ch);
static void print_hex_dump(const char *prefix, const void *buf, size_t len);
static FILE *create_reset_single_image(void);
static int check_quec_usb_desc(int fd, struct qdl_device *qdl, int *intf);

const char *boot_sahara_cmd_id_str[QUEC_SAHARA_FW_UPDATE_END_ID+1] = {
        "SAHARA_NO_CMD_ID",               // = 0x00,
        " SAHARA_HELLO_ID",               // = 0x01, // sent from target to host
        "SAHARA_HELLO_RESP_ID",           // = 0x02, // sent from host to target
        "SAHARA_READ_DATA_ID",            // = 0x03, // sent from target to host
        "SAHARA_END_IMAGE_TX_ID",         // = 0x04, // sent from target to host
        "SAHARA_DONE_ID",                 // = 0x05, // sent from host to target
        "SAHARA_DONE_RESP_ID",            // = 0x06, // sent from target to host
        "SAHARA_RESET_ID",                // = 0x07, // sent from host to target
        "SAHARA_RESET_RESP_ID",           // = 0x08, // sent from target to host
        "SAHARA_MEMORY_DEBUG_ID",         // = 0x09, // sent from target to host
        "SAHARA_MEMORY_READ_ID",          // = 0x0A, // sent from host to target
        "SAHARA_CMD_READY_ID",            // = 0x0B, // sent from target to host
        "SAHARA_CMD_SWITCH_MODE_ID",      // = 0x0C, // sent from host to target
        "SAHARA_CMD_EXEC_ID",             // = 0x0D, // sent from host to target
        "SAHARA_CMD_EXEC_RESP_ID",        // = 0x0E, // sent from target to host
        "SAHARA_CMD_EXEC_DATA_ID",        // = 0x0F, // sent from host to target
        "SAHARA_64_BITS_MEMORY_DEBUG_ID", // = 0x10, // sent from target to host
        "SAHARA_64_BITS_MEMORY_READ_ID",  // = 0x11, // sent from host to target
        "SAHARA_64_BITS_READ_DATA_ID",    // = 0x12,
        "NOP",                            // = 0x13,
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "NOP",
        "QUEC_SAHARA_FW_UPDATE_PROCESS_REPORT_ID",
        "QUEC_SAHARA_FW_UPDATE_END_ID"
};

uint32_t le_uint32(uint32_t v32)
{
    const uint32_t is_bigendian = 1;
    uint32_t tmp = v32;
    if ((*(uint8_t *)&is_bigendian) == 0)
    {
        uint8_t *s = (uint8_t *)(&v32);
        uint8_t *d = (uint8_t *)(&tmp);
        d[0] = s[3];
        d[1] = s[2];
        d[2] = s[1];
        d[3] = s[0];
    }
    return tmp;
}

static uint8_t to_hex(uint8_t ch)
{
    ch &= 0xf;
    return ch <= 9 ? '0' + ch : 'a' + ch - 10;
}


static void print_hex_dump(const char *prefix, const void *buf, size_t len)
{
    const uint8_t *ptr = buf;
    size_t linelen;
    uint8_t ch;
    char line[16 * 3 + 16 + 1];
    int li;
    size_t i;
    size_t j;

    for (i = 0; i < len; i += 16)
    {
        linelen = MIN(16, len - i);
        li = 0;

        for (j = 0; j < linelen; j++)
        {
            ch = ptr[i + j];
            line[li++] = to_hex(ch >> 4);
            line[li++] = to_hex(ch);
            line[li++] = ' ';
        }

        for (; j < 16; j++)
        {
            line[li++] = ' ';
            line[li++] = ' ';
            line[li++] = ' ';
        }

        for (j = 0; j < linelen; j++)
        {
            ch = ptr[i + j];
            line[li++] = isprint(ch) ? ch : '.';
        }

        line[li] = '\0';

        printf("%s %04zx: %s\n", prefix, i, line);
    }
}


FILE *create_reset_single_image(void)
{
    int fd;
    FILE *fp = NULL;

    struct single_image_hdr *img_hdr;

    img_hdr = malloc(SINGLE_IMAGE_HDR_SIZE);
    if (img_hdr == NULL)
        return NULL;

    memset(img_hdr, 0, SINGLE_IMAGE_HDR_SIZE);

    img_hdr->magic[0] = 'R';
    img_hdr->magic[1] = 'S';
    img_hdr->magic[2] = 'T';

    fd = open("/tmp", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
    if (fd < 0)
        goto EXIT;

    fp = fdopen(fd, "w+");

    if (fwrite(img_hdr, 1, SINGLE_IMAGE_HDR_SIZE, fp) != SINGLE_IMAGE_HDR_SIZE)
    {
        dbg("fail to write\n");
        fclose(fp);
        fp = NULL;
    }
    else 
    {
        fseek(fp, 0, SEEK_SET);
    }

EXIT:
    free(img_hdr);
    return fp;
}


int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
    struct usbdevfs_bulktransfer bulk = {};
    bulk.ep = qdl->in_ep;
    bulk.len = len;
    bulk.data = buf;
    bulk.timeout = timeout;
    return ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len)
{
    unsigned char *data = (unsigned char*) buf;
    struct usbdevfs_bulktransfer bulk = {};
    unsigned count = 0;
    size_t len_orig = len;
    int n;
    while(len > 0)
    {
        int xfer;
        xfer = (len > qdl->out_maxpktsize) ? qdl->out_maxpktsize : len;

        bulk.ep = qdl->out_ep;
        bulk.len = xfer;
        bulk.data = data;
        bulk.timeout = 1000;

        n = ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
        if(n != xfer)
        {
            fprintf(stderr, "ERROR: n = %d, errno = %d (%s)\n", n, errno, strerror(errno));
            return -1;
        }
        count += xfer;
        len -= xfer;
        data += xfer;
    }    
    if (len_orig % qdl->out_maxpktsize == 0)
    {
        bulk.ep = qdl->out_ep;
        bulk.len = 0;
        bulk.data = NULL;
        bulk.timeout = 1000;

        n = ioctl(qdl->fd, USBDEVFS_BULK, &bulk);
        if (n < 0)
            return n;
    }
    return count;
}

int qdl_close(struct qdl_device *qdl)
{
    int bInterfaceNumber = 3;
    ioctl(qdl->fd, USBDEVFS_RELEASEINTERFACE, &bInterfaceNumber);
    close(qdl->fd);
    return 0;
}

int qdl_open(struct qdl_device *qdl)
{
    struct udev_enumerate *enumerate;
    struct udev_list_entry *devices;
    struct udev_list_entry *dev_list_entry;
    struct udev_monitor *mon;
    struct udev_device *dev;
    const char *dev_node;
    struct udev *udev;
    const char *path;
    struct usbdevfs_ioctl cmd;
    int intf = -1;
    int ret;
    int fd;

    udev = udev_new();
    if (!udev)
        err(1, "failed to initialize udev");

    mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "usb", NULL);
    udev_monitor_enable_receiving(mon);

    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "usb");
    udev_enumerate_scan_devices(enumerate);
    devices = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, devices)
    {
        path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);
        dev_node = udev_device_get_devnode(dev);

        if (!dev_node)
        {
            continue;
        }
        fd = open(dev_node, O_RDWR);
        if (fd < 0)
            continue;
        dbg_time("D: %s \n", dev_node);
        ret = check_quec_usb_desc(fd, qdl, &intf);
        if (!ret)
        {
            goto found;
        }
        close(fd);
    }

    udev_enumerate_unref(enumerate);
    udev_monitor_unref(mon);
    udev_unref(udev);
    return -ENOENT;

found:
    udev_enumerate_unref(enumerate);
    udev_monitor_unref(mon);
    udev_unref(udev);

    cmd.ifno = intf;
    cmd.ioctl_code = USBDEVFS_DISCONNECT;
    cmd.data = NULL;

    ret = ioctl(qdl->fd, USBDEVFS_IOCTL, &cmd);
    if (ret && errno != ENODATA)
        err(1, "failed to disconnect kernel driver");

    ret = ioctl(qdl->fd, USBDEVFS_CLAIMINTERFACE, &intf);
    if (ret < 0)
        err(1, "failed to claim USB interface");

    return 0;
}

int sahara_rx_data(struct qdl_device *qdl, void *rx_buffer, size_t bytes_to_read)
{
    struct sahara_pkt * cmd_packet_header = NULL;
    size_t bytes_read = 0;
    if (!bytes_to_read)
    {
        bytes_read = qdl_read(qdl, rx_buffer, sizeof(struct sahara_pkt), 5000);
        cmd_packet_header = (struct sahara_pkt *)rx_buffer;
        dbg("RECEIVED <-- %s %zx bytes", boot_sahara_cmd_id_str[le_uint32(cmd_packet_header->cmd)], bytes_read);
        return bytes_read;
    }

    return 0;
}

int check_quec_usb_desc(int fd, struct qdl_device *qdl, int *intf)
{
    const struct usb_interface_descriptor *ifc;
    const struct usb_endpoint_descriptor *ept;
    const struct usb_device_descriptor *dev;
    const struct usb_config_descriptor *cfg;
    const struct usb_descriptor_header *hdr;
    unsigned type;
    unsigned out;
    unsigned in;
    unsigned k;
    unsigned l;
    ssize_t n;
    size_t out_size;
    size_t in_size;
    void *ptr;
    void *end;
    char desc[1024];

    n = read(fd, desc, sizeof(desc));
    if (n < 0)
    {
        return n;
    }
    ptr = (void*)desc;
    end = ptr + n;
    dev = ptr;

    /* Consider only devices with vid 0x2c7c */
    if ((dev->idVendor != 0x2c7c) && (dev->idVendor != 0x05c6)) 
    {
        return -EINVAL;
    }
    else
    {
        if (dev->idProduct == 9008)
        {
            return SWITCHED_TO_EDL;
        }
    }

    dbg_time("D: idVendor=%04x idProduct=%04x\n",  dev->idVendor, dev->idProduct);
    ptr += dev->bLength;

    if (ptr >= end || dev->bDescriptorType != USB_DT_DEVICE)
        return -EINVAL;

    cfg = ptr;
    ptr += cfg->bLength;
    if (ptr >= end || cfg->bDescriptorType != USB_DT_CONFIG)
        return -EINVAL;

    unsigned numInterfaces = cfg->bNumInterfaces;
    dbg_time("C: bNumInterfaces: %d\n", numInterfaces);

    if (numInterfaces <= 0 || numInterfaces > MAX_NUM_INTERFACES)
    {
        printf("invalid no of interfaces: %d\n", numInterfaces);
        return -EINVAL;
    }
    for (k = 0; k < numInterfaces; k++)
    {
        if (ptr >= end)
        {
            return -EINVAL;
        }

        do
        {
            ifc = ptr;
            if (ifc->bLength < USB_DT_INTERFACE_SIZE)
            {
                printf("Exiting here ifc->bLengh:%d Interface size: %d\n", ifc->bLength, USB_DT_INTERFACE_SIZE);
            }
            ptr += ifc->bLength;

        } while (ptr < end && ifc->bDescriptorType != USB_DT_INTERFACE);

        dbg_time("I: If#= %d Alt= %d #EPs= %d Cls=%02x Sub=%02x Prot=%02x\n",
                ifc->bInterfaceNumber, ifc->bAlternateSetting,
                ifc->bNumEndpoints, ifc->bInterfaceClass,
                ifc->bInterfaceSubClass, ifc->bInterfaceProtocol);
        in = -1;
        out = -1;
        in_size = 0;
        out_size = 0;

        unsigned noOfEndpoints = ifc->bNumEndpoints;
        if (noOfEndpoints <= 0 || noOfEndpoints > MAX_NUM_ENDPOINTS)
        {
            printf("invalid no of endpoints: %d\n", noOfEndpoints);
            continue;
        }
        for (l = 0; l < noOfEndpoints; l++)
        {
            if (ptr >= end)
            {
                printf("%s %d end has been reached\n",__FILE__, __LINE__);
                return -EINVAL;
            }

            do
            {
                ept = ptr;
                if (ept->bLength < USB_DT_ENDPOINT_SIZE)
                {
                    printf("%s %d endpoint length:%d expected size: %d \n",__FILE__, __LINE__, ept->bLength,  USB_DT_ENDPOINT_SIZE);
                    return -EINVAL;
                }
                ptr += ept->bLength;
            } while (ptr < end && ept->bDescriptorType != USB_DT_ENDPOINT);

            dbg_time("E: Ad=%02x Atr=%02x MxPS= %d Ivl=%dms\n",
                ept->bEndpointAddress,
                ept->bmAttributes,
                ept->wMaxPacketSize,
                ept->bInterval);

            type = ept->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
            if (type != USB_ENDPOINT_XFER_BULK)
                continue;

            if (ept->bEndpointAddress & USB_DIR_IN)
            {
                in = ept->bEndpointAddress;
                in_size = ept->wMaxPacketSize;
            }
            else
            {
                out = ept->bEndpointAddress;
                out_size = ept->wMaxPacketSize;
            }

            if (ptr >= end)
                break;

            hdr = ptr;
            if (hdr->bDescriptorType == USB_DT_SS_ENDPOINT_COMP)
                ptr += USB_DT_SS_EP_COMP_SIZE;
        }

        if (ifc->bInterfaceClass != 0xff)
            continue;

        if (ifc->bInterfaceSubClass != 0xff)
            continue;

        if (ifc->bInterfaceProtocol != 0xff &&
            ifc->bInterfaceProtocol != 16 &&
            ifc->bInterfaceProtocol != 17)
            continue;

        qdl->fd = fd;
        qdl->in_ep = in;
        qdl->out_ep = out;
        qdl->in_maxpktsize = in_size;
        qdl->out_maxpktsize = out_size;

        if( qdl->in_maxpktsize <= 0 || qdl->out_maxpktsize <= 0 )
        {
            printf("%s %d invalid max packet size received.\n",__FILE__, __LINE__);
            return -ENOENT;
        }
        
        *intf = ifc->bInterfaceNumber;

        return SWITCHED_TO_SBL;
    }

    return -ENOENT;
}


static void sahara_hello_multi(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
    struct sahara_pkt resp;

    assert(pkt->length == 0x30);
    resp.cmd = 2;
    resp.length = 0x30;
    resp.hello_resp.version = 2;
    resp.hello_resp.compatible = pkt->hello_req.compatible;
    resp.hello_resp.status = 0;
    resp.hello_resp.mode = 0x10; // Super Special Quectel mode

    qdl_write(qdl, &resp, resp.length);
    return;
}

int start_image_transfer(struct qdl_device *qdl ,
            struct sahara_pkt* sahara_read_data,
            char *file_name)
{
    int retval = 0;
    void* tx_buffer;
    FILE* file_handle;
    uint32_t file_offset = 0;
    size_t file_read;
    struct single_image_hdr *img_hdr = NULL;
    uint32_t bytes_read = 0, bytes_to_read_next;
    uint32_t DataOffset = 0;
    uint32_t DataLength = 0;

    if (qdl == NULL )
    {
        return -2;
    }

    if (sahara_read_data == NULL)
    {
        return -3;
    }

    DataOffset = le_uint32(sahara_read_data->read_req.offset);
    DataLength = le_uint32(sahara_read_data->read_req.length);

    img_hdr = (struct single_image_hdr *)malloc(SINGLE_IMAGE_HDR_SIZE);
    if (!img_hdr)
    {
        return -4;
    }

    tx_buffer = malloc(SAHARA_RAW_BUFFER_SIZE);
    if (!tx_buffer)
    {
        free(img_hdr);
        return -4;
    }

    if ( file_name != NULL )
    {
        file_handle = fopen(file_name, "rb");
    }
    else
    {
        file_handle = create_reset_single_image();
    }


    if (!file_handle)
    {
        free(img_hdr);
        free(tx_buffer);
        return -5;
    }


    file_read = fread(img_hdr, 1, (SINGLE_IMAGE_HDR_SIZE), file_handle);
    if (file_read != (SINGLE_IMAGE_HDR_SIZE))
    {
        free(img_hdr);
        free(tx_buffer);
        fclose(file_handle);
        return -6;
    }

    /* Reset the file pointer to get it ready for delivery*/
    fseek(file_handle, 0, SEEK_SET);

    dbg("%s:Img id: 0x%08x Offset: 0x%08x Len: 0x%08x", __FUNCTION__ ,le_uint32(sahara_read_data->read_req.image), DataOffset, DataLength);

    if (fseek(file_handle, file_offset + (long)DataOffset, SEEK_SET))
    {
        dbg("%d errno: %d (%s)", __LINE__, errno, strerror(errno));
        free(img_hdr);
        free(tx_buffer);
        fclose(file_handle);
        return 0;
    }

    while (bytes_read < DataLength)
    {
        bytes_to_read_next = MIN((uint32_t)DataLength - bytes_read, SAHARA_RAW_BUFFER_SIZE);
        retval = fread(tx_buffer, 1, bytes_to_read_next, file_handle);

        if (retval < 0)
        {
            dbg("file read failed: %s", strerror(errno));
            free(img_hdr);
            free(tx_buffer);
            fclose(file_handle);
            return 0;
        }

        if ((uint32_t)retval != bytes_to_read_next)
        {
            dbg("Read %d bytes, but was asked for 0x%08x bytes", retval, DataLength);
            free(img_hdr);
            free(tx_buffer);
            fclose(file_handle);
            return 0;
        }

        /*send the image data*/
        if (0 == qdl_write(qdl, tx_buffer, bytes_to_read_next))
        {
            dbg("Tx Sahara Image Failed");
            free(img_hdr);
            free(tx_buffer);
            fclose(file_handle);
            return 0;
        }

        bytes_read += bytes_to_read_next;
    }
    free(tx_buffer);
    free(img_hdr);
    fclose(file_handle);
    return 1;
}


int sahara_flash_all(char *main_file_path, char *oem_file_path, char *carrier_file_path)
{
    int ret;
    int i, count;

    struct qdl_device qdl;
    struct sahara_pkt *pspkt;
    char buffer[QBUFFER_SIZE];
    int nBytes = 0;
    char * files[4];
    char * current_file_name;
    bool done = false;
    ret = qdl_open(&qdl);

    if (ret)
    {
        printf("Could not find a Quectel device ready to flash!\n");
        return -1;
    }
    else
    {
        printf("%s: Found a Quectel device ready to flash!\n",__FUNCTION__);
    }

    count = 0;
    if ( strlen(main_file_path) )
        files[count++] = main_file_path;

    if ( strlen(carrier_file_path) )
        files[count++] = carrier_file_path;

    if ( strlen(oem_file_path) )
        files[count++] = oem_file_path;

    if (!count) {
	    qdl_close(&qdl);
	    return -1;
    }
    files[count++] = NULL; // for rest image

    memset(buffer, 0 , QBUFFER_SIZE );
    nBytes = sahara_rx_data(&qdl, buffer, 0);
    pspkt = (struct sahara_pkt *)buffer;

    if (le_uint32(pspkt->cmd) != 0x01)
    {
        dbg("Received a different command: %x while waiting for hello packet \n Bytes received %d\n", pspkt->cmd, nBytes);
        qdl_close(&qdl);
        return -1;
    }

    sahara_hello_multi(&qdl, pspkt);

    for(i = 0; i < count; i++)
    {
        current_file_name = files[i];
        if (current_file_name) {
            printf("\nFlashing : %s\n", current_file_name);
        } else {
            printf("\nFlashing reset image\n");
        }
	done = false;
        while(!done) {
            memset(buffer, 0 , QBUFFER_SIZE );
            nBytes = sahara_rx_data(&qdl, buffer, 0);
            if (nBytes < 0)
            {
                continue;
            }
            pspkt = (struct sahara_pkt *)buffer;
            if ((uint32_t)nBytes != pspkt->length)
            {
                fprintf(stderr, "Sahara pkt length not matching");
                return -EINVAL;
            }

            if (pspkt->cmd == 3)
            {
                start_image_transfer(&qdl , pspkt , current_file_name);
                continue;
            }
            if  (pspkt->cmd == QUEC_SAHARA_FW_UPDATE_PROCESS_REPORT_ID)
            {
                dbg("Writing %d percent %c", le_uint32(pspkt->packet_fw_update_process_report.percent), (le_uint32(pspkt->packet_fw_update_process_report.percent == 100) ? '\n' : '\r'));
                continue;
            }

            if (pspkt->cmd == QUEC_SAHARA_FW_UPDATE_END_ID)
            {
                if (le_uint32(pspkt->packet_fw_update_end.successful))
                    dbg("firmware flash error (%d)", le_uint32(pspkt->packet_fw_update_end.successful));
                else
                {
                    dbg("firmware flash successful");
                }
                done = true;
            }
        }
    }
    qdl_close(&qdl);
    return 0;
}
