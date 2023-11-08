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
#include "ql-qdl-firehose.h"


char *q_device_type = "nand";
SparseImgParam SparseImgData;

static int usbfs_bulk_write(struct qdl_device *qdl, const void *data, int len, int timeout_msec, int need_zlp) {
    struct usbdevfs_urb bulk;
    struct usbdevfs_urb *urb = &bulk;
    int n = -1;

    (void)timeout_msec;
    memset(urb, 0, sizeof(struct usbdevfs_urb));
    urb->type = USBDEVFS_URB_TYPE_BULK;
    urb->endpoint = qdl->out_ep;
    urb->status = -1;
    urb->buffer = (void *)data;
    urb->buffer_length = len;
    urb->usercontext = urb;

    if (need_zlp && (len%qdl->out_maxpktsize) == 0) {
        //dbg_time("USBDEVFS_URB_ZERO_PACKET\n");
#ifndef USBDEVFS_URB_ZERO_PACKET
#define USBDEVFS_URB_ZERO_PACKET    0x40
#endif
      urb->flags = USBDEVFS_URB_ZERO_PACKET;
    } else {
        urb->flags = 0;
    }

    do {
        n = ioctl(qdl->fd, USBDEVFS_SUBMITURB, urb);
    } while((n < 0) && (errno == EINTR));

    if (n != 0) {
        printf(" USBDEVFS_SUBMITURB %d/%d, errno = %d (%s)\n",  n, urb->buffer_length, errno, strerror(errno));
        return -1;
    }

    do {
        urb = NULL;
        n = ioctl(qdl->fd, USBDEVFS_REAPURB, &urb);
    } while((n < 0) && (errno == EINTR));

    if (n != 0) {
        printf("out_ep %d/%d, errno = %d (%s)\n", n, urb->buffer_length, errno, strerror(errno));
    }

    //dbg_time("[ urb @%p status = %d, actual = %d ]\n", urb, urb->status, urb->actual_length);

    if (urb && urb->status == 0 && urb->actual_length)
        return urb->actual_length;

    return -1;
}



static const char * fh_xml_find_value(const char *xml_line, const char *key, char **ppend)
{
  char *pchar = strstr(xml_line, key);
  char *pend;

  if (!pchar) {
    printf("%s: no key %s in %s\n", __func__, key, xml_line);
    return NULL;
  }

  pchar += strlen(key);
  if (pchar[0] != '=' && pchar[1] != '"') {
    printf("%s: no start %s in %s\n", __func__, "=\"", xml_line);
    return NULL;
  }

  pchar += strlen("=\"");
  pend = strstr(pchar, "\"");
  if (!pend) {
    printf("%s: no end %s in %s\n", __func__, "\"", xml_line);
    return NULL;
  }

  *ppend = pend;
  return pchar;
}

static void fh_xml_set_value(char *xml_line, const char *key, unsigned value) {
  char *pend;
  const char *pchar = fh_xml_find_value(xml_line, key, &pend);
  char value_str[32];
  char *tmp_line = malloc(strlen(xml_line) + 1 + sizeof(value_str));

  if (!pchar || !tmp_line) {
    return;
  }

  strcpy(tmp_line, xml_line);

  snprintf(value_str, sizeof(value_str), "%u", value);
  tmp_line[pchar - xml_line] = '\0';
  strcat(tmp_line, value_str);
  strcat(tmp_line, pend);
  strcpy(xml_line, tmp_line);
  free(tmp_line);
}


static const char * fh_xml_get_value(const char *xml_line, const char *key)
{
  static char value[64];
  char *pend;
  const char *pchar = fh_xml_find_value(xml_line, key, &pend);
  if (!pchar) {
    return NULL;
  }
  strncpy(value, pchar, pend - pchar);
  value[pend - pchar] = '\0';
  return value;
}


static int fh_parse_xml_line(const char *xml_line, struct fh_cmd *fh_cmd) {
  const char *pchar = NULL;
  size_t len = strlen(xml_line);

  memset(fh_cmd, 0, sizeof( struct fh_cmd));
  strcpy(fh_cmd->xml_original_data, xml_line);
  if (fh_cmd->xml_original_data[len - 1] == '\n')
    fh_cmd->xml_original_data[len - 1] = '\0';

  if (strstr(xml_line, "vendor=\"quectel\"")) {
    fh_cmd->vdef.type = "vendor";
    return 0;
  }
  else if (!strncmp(xml_line, "<erase ", strlen("<erase "))) {
    fh_cmd->erase.type = "erase";
    if (strstr(xml_line, "last_sector")) {
      if ((pchar = fh_xml_get_value(xml_line, "last_sector")))
        fh_cmd->erase.last_sector = atoi(pchar);
    }
    if ((pchar = fh_xml_get_value(xml_line, "start_sector")))
      fh_cmd->erase.start_sector = atoi(pchar);
    if ((pchar = fh_xml_get_value(xml_line, "num_partition_sectors")))
      fh_cmd->erase.num_partition_sectors = atoi(pchar);
    if ((pchar = fh_xml_get_value(xml_line, "SECTOR_SIZE_IN_BYTES")))
      fh_cmd->erase.SECTOR_SIZE_IN_BYTES = atoi(pchar);

    return 0;
  }
  else if (!strncmp(xml_line, "<program ", strlen("<program "))) {
    fh_cmd->program.type = "program";
    if ((pchar = fh_xml_get_value(xml_line, "filename")))
      {
        fh_cmd->program.filename = strdup(pchar);
        if(fh_cmd->program.filename[0] == '\0')
          {//some fw version have blank program line, ignore it.
            return -1;
          }
      }

    if ((pchar = fh_xml_get_value(xml_line, "sparse")))  {
        fh_cmd->program.sparse = strdup(pchar);
    }  else {
      fh_cmd->program.sparse = NULL;
    }

    if ((pchar = fh_xml_get_value(xml_line, "start_sector")))
      fh_cmd->program.start_sector = atoi(pchar);
    if ((pchar = fh_xml_get_value(xml_line, "num_partition_sectors")))
      fh_cmd->program.num_partition_sectors = atoi(pchar);
    if ((pchar = fh_xml_get_value(xml_line, "SECTOR_SIZE_IN_BYTES")))
      fh_cmd->program.SECTOR_SIZE_IN_BYTES = atoi(pchar);

    if (fh_cmd->program.sparse != NULL && !strncasecmp(fh_cmd->program.sparse, "true", 4))
      {
        if ((pchar = fh_xml_get_value(xml_line, "file_sector_offset")))
          fh_cmd->program.file_sector_offset = atoi(pchar);
        if ((pchar = fh_xml_get_value(xml_line, "physical_partition_number")))
          fh_cmd->program.physical_partition_number = atoi(pchar);
      }

    return 0;
  }
  else if (!strncmp(xml_line, "<patch ", strlen("<patch "))) {
    fh_cmd->patch.type = "patch";
    pchar = fh_xml_get_value(xml_line, "filename");
    if (pchar && strcmp(pchar, "DISK"))
      return -1;
    return 0;
  }
  else if (!strncmp(xml_line, "<response ", strlen("<response "))) {
    fh_cmd->response.type = "response";
    pchar = fh_xml_get_value(xml_line, "value");
    if (pchar) {
      if (!strcmp(pchar, "ACK"))
        fh_cmd->response.value =  "ACK";
      else if(!strcmp(pchar, "NAK"))
        fh_cmd->response.value =  "NAK";
      else
        fh_cmd->response.value =  "OTHER";
    }
    if (strstr(xml_line, "rawmode")) {
      pchar = fh_xml_get_value(xml_line, "rawmode");
      if (pchar) {
        fh_cmd->response.rawmode = !strcmp(pchar, "true");
      }
    }
    else if (strstr(xml_line, "MaxPayloadSizeToTargetInBytes")) {
      pchar = fh_xml_get_value(xml_line, "MaxPayloadSizeToTargetInBytes");
      if (pchar) {
        fh_cmd->response.MaxPayloadSizeToTargetInBytes = atoi(pchar);
      }
    }
    return 0;
  }
  else if (!strncmp(xml_line, "<log ", strlen("<log "))) {
    fh_cmd->program.type = "log";
    return 0;
  }

  return -1;
}


static int fh_validate_program_cmd(struct fh_data *fh_data, struct fh_cmd *fh_cmd)
{
    char full_path[512];
    char *unix_filename = strdup(fh_cmd->program.filename);
    char *ptmp;
    FILE *fp;
    long filesize = 0;
    uint32_t num_partition_sectors = fh_cmd->program.num_partition_sectors;

    while((ptmp = strchr(unix_filename, '\\'))) {
        *ptmp = '/';
    }

    snprintf(full_path, sizeof(full_path), "%.255s/%.240s", fh_data->firehose_dir, unix_filename);
    if (access(full_path, R_OK)) {
        fh_cmd->program.num_partition_sectors = 0;
        printf("failed to access %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        return -1;
    }

    fp = fopen(full_path, "rb");
    if (!fp) {
        fh_cmd->program.num_partition_sectors = 0;
        printf("failed to fopen %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        return -2;
    }

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    fclose(fp);

    if (filesize <= 0) {
        printf("failed to ftell %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        fh_cmd->program.num_partition_sectors = 0;
        fh_cmd->program.filesz = 0;
        return -3;
    }
    fh_cmd->program.filesz = filesize;
    fh_cmd->program.num_partition_sectors = filesize/fh_cmd->program.SECTOR_SIZE_IN_BYTES;
    if (filesize%fh_cmd->program.SECTOR_SIZE_IN_BYTES)
        fh_cmd->program.num_partition_sectors += 1;

    if (!strncasecmp(unix_filename, "gpt_empty0.bin", 14))
    {
        fh_cmd->program.num_partition_sectors -= 1;
    }

    if (num_partition_sectors != fh_cmd->program.num_partition_sectors) {
        fh_xml_set_value(fh_cmd->xml_original_data, "num_partition_sectors",
            fh_cmd->program.num_partition_sectors);
    }

    free(unix_filename);

    return 0;
}





static int fh_recv_cmd(struct fh_data *fh_data, struct fh_cmd *fh_cmd,
                       unsigned timeout)
{
  size_t bytes_read = 0;
  char *xml_line;
  char *pend;

  memset(fh_cmd, 0, sizeof(struct fh_cmd));
  fh_data->xml_rx_buf[bytes_read] = '\0';

  bytes_read = qdl_read(fh_data->usb_handle,
                        fh_data->xml_rx_buf,
                        fh_data->xml_rx_size, timeout);

  if ( bytes_read <= 0) {
    return -1;
  }


  xml_line = fh_data->xml_rx_buf;

  while (*xml_line) {
    xml_line = strstr(xml_line, "<?xml version=");
    if (xml_line == NULL) {
      if (fh_cmd->cmd.type == 0) {
        printf("{{{%s}}}", fh_data->xml_rx_buf);
        return -1;
      } else {
        break;
      }
    }
    xml_line += strlen("<?xml version=");

    xml_line = strstr(xml_line, "<data>");
    if (xml_line == NULL) {
      printf("{{{%s}}}", fh_data->xml_rx_buf);
      return -2;
    }
    xml_line += strlen("<data>");
    if (xml_line[0] == '\n')
      xml_line++;

    if (!strncmp(xml_line, "<response ", strlen("<response "))) {
      fh_parse_xml_line(xml_line, fh_cmd);
      pend = strstr(xml_line, "/>");
      pend += 2;
      printf("%.*s\n", (int)(pend -xml_line),  xml_line);
      xml_line = pend + 1;
    }
    else if (!strncmp(xml_line, "<log ", strlen("<log "))) {
      if (fh_cmd->cmd.type && strcmp(fh_cmd->cmd.type, "log")) {
        printf("{{{%s}}}", fh_data->xml_rx_buf);
        break;
      }
      fh_parse_xml_line(xml_line, fh_cmd);
      pend = strstr(xml_line, "/>");
      pend += 2;
      {
        char *prn = xml_line;
        while (prn < pend) {
          if (*prn == '\r' || *prn == '\n')
            *prn = '.';
          prn++;
        }
      }
      printf("%.*s\n", (int)(pend -xml_line),  xml_line);
      xml_line = pend + 1;
    } else {
      printf("unknown %s", xml_line);
      return -3;
    }
  }

  if (fh_cmd->cmd.type) {

    return 0;
  }


  return bytes_read;
}

static int fh_wait_response_cmd(struct fh_data *fh_data, struct fh_cmd *fh_cmd, unsigned timeout)
{
  while (1) {
    int ret = fh_recv_cmd(fh_data, fh_cmd, timeout);
    if ( ret !=0 ) {
      continue;
      sleep(1);
    }
    if (strstr(fh_cmd->cmd.type, "log")) {
      continue;
    }
    return 0;
  }

  return -1;
}

static int fh_send_cmd(struct fh_data *fh_data, const struct fh_cmd *fh_cmd)
{
    int tx_len = 0;

    char *pstart, *pend;
    char *xml_buf = fh_data->xml_tx_buf;
    //    char* recv_xml_buf = fh_data->xml_rx_buf;
    unsigned xml_size = fh_data->xml_tx_size;
    xml_buf[0] = '\0';

    snprintf(xml_buf + strlen(xml_buf), xml_size, "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n");
    snprintf(xml_buf + strlen(xml_buf), xml_size, "<data>\n");
    pstart = xml_buf + strlen(xml_buf);

    if (!strcmp(fh_cmd->cmd.type, "vendor")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "%s", fh_cmd->xml_original_data);
    }
    else if (!strcmp(fh_cmd->cmd.type, "erase")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "%s", fh_cmd->xml_original_data);
    }
    else if (!strcmp(fh_cmd->cmd.type, "program")) {
        if (fh_cmd->program.sparse != NULL && !strncasecmp(fh_cmd->program.sparse, "true", 4)) {
            snprintf(xml_buf + strlen(xml_buf), xml_size,
                "<program filename=\"%.120s\" SECTOR_SIZE_IN_BYTES=\"%d\" num_partition_sectors=\"%d\" physical_partition_number=\"%d\" start_sector=\"%d\" file_sector_offset=\"%d\" sparse=\"%.120s\" UNSPARSE_FILE_SIZE=\"%d\" />",
                fh_cmd->program.filename, fh_cmd->program.SECTOR_SIZE_IN_BYTES, fh_cmd->program.num_partition_sectors, fh_cmd->program.physical_partition_number, fh_cmd->program.start_sector, fh_cmd->program.file_sector_offset, fh_cmd->program.sparse, fh_cmd->program.UNSPARSE_FILE_SIZE);
        } else {
          snprintf(xml_buf + strlen(xml_buf), xml_size, "%s", fh_cmd->xml_original_data);
        }
    } else if (!strcmp(fh_cmd->cmd.type, "patch")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "%s", fh_cmd->xml_original_data);
    } else if (!strcmp(fh_cmd->cmd.type, "configure")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size,
            "<configure MemoryName=\"%.8s\" Verbose=\"%d\" AlwaysValidate=\"%d\" MaxDigestTableSizeInBytes=\"%d\" MaxPayloadSizeToTargetInBytes=\"%d\"  ZlpAwareHost=\"%d\" SkipStorageInit=\"%d\" />",
            fh_cmd->cfg.MemoryName, fh_cmd->cfg.Verbose, fh_cmd->cfg.AlwaysValidate,
            fh_cmd->cfg.MaxDigestTableSizeInBytes,
            fh_cmd->cfg.MaxPayloadSizeToTargetInBytes,
            fh_cmd->cfg.ZlpAwareHost, fh_cmd->cfg.SkipStorageInit);
    } else if (!strcmp(fh_cmd->cmd.type, "setbootablestoragedrive")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "<setbootablestoragedrive value=\"%d\" />",
            !strcmp(q_device_type, "ufs") ? 1 : 0);
    } else if (!strcmp(fh_cmd->cmd.type, "reset")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "<power DelayInSeconds=\"%u\" value=\"reset\" />", 10);
    } else {
        printf("%s unkonw fh_cmd->cmd.type=%s\n", __func__, fh_cmd->cmd.type);
        return -1;
    }

    pend = xml_buf + strlen(xml_buf);
    printf("%.*s\n", (int)(pend - pstart),  pstart);
    //snprintf(xml_buf + strlen(xml_buf), xml_size, "\n</data>");

    if (!strcmp(fh_cmd->cmd.type, "setbootablestoragedrive") || !strcmp(fh_cmd->cmd.type, "reset")
       || !strcmp(fh_cmd->cmd.type, "configure")) {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "\n</data>\n");
    } else {
        snprintf(xml_buf + strlen(xml_buf), xml_size, "\n</data>");
    }

    tx_len = usbfs_bulk_write(fh_data->usb_handle , xml_buf , strlen(xml_buf), 1000, 1);

    if ((size_t)tx_len == strlen(xml_buf)) {

      return 0;
    }
    return -2;
}

static int fh_send_cfg_cmd(struct fh_data *fh_data) {
  struct fh_cmd fh_cfg_cmd;
  struct fh_cmd fh_rx_cmd;

  int ret = 0;
  memset(&fh_cfg_cmd, 0x00, sizeof(fh_cfg_cmd));
  fh_cfg_cmd.cfg.type = "configure";
  fh_cfg_cmd.cfg.MemoryName = q_device_type;
  fh_cfg_cmd.cfg.Verbose = 0;
  fh_cfg_cmd.cfg.AlwaysValidate = 0;
  fh_cfg_cmd.cfg.SkipStorageInit = 0;
  fh_cfg_cmd.cfg.ZlpAwareHost = fh_data->ZlpAwareHost; 

  fh_cfg_cmd.cfg.MaxDigestTableSizeInBytes = 2048;
  fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes = 8192;
  fh_cfg_cmd.cfg.MaxPayloadSizeFromTargetInBytes = 2048;
  fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInByteSupported = 8192;


  ret = fh_send_cmd(fh_data, &fh_cfg_cmd);
  if (ret) {
    printf("FIREHOSE: %s, %d  send configuration command\n", __FUNCTION__, __LINE__);
    return -1;
  }

  if (fh_wait_response_cmd(fh_data, &fh_rx_cmd, 5000) != 0) {
    printf("FIREHOSE: %s, %d did not get a response", __FUNCTION__, __LINE__);
    return -2;
  }
    

  if (!strcmp(fh_rx_cmd.response.value, "NAK") && fh_rx_cmd.response.MaxPayloadSizeToTargetInBytes) {
    fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes = fh_rx_cmd.response.MaxPayloadSizeToTargetInBytes;
    fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInByteSupported = fh_rx_cmd.response.MaxPayloadSizeToTargetInBytes;
    
    fh_send_cmd(fh_data, &fh_cfg_cmd);
    if (fh_wait_response_cmd(fh_data, &fh_rx_cmd, 5000) != 0) {
      return -3;
    }
  }
  if (strcmp(fh_rx_cmd.response.value, "ACK") != 0) {
    return -4;
  }

  fh_data->MaxPayloadSizeToTargetInBytes = fh_cfg_cmd.cfg.MaxPayloadSizeToTargetInBytes;

  return 0;
}


static int fh_process_erase(struct fh_data *fh_data, const struct fh_cmd *fh_cmd)
{
  struct fh_cmd fh_rx_cmd;
  unsigned timeout = 15000; //8+8 MCP need more time

  fh_send_cmd(fh_data, fh_cmd);

  if (fh_wait_response_cmd(fh_data, &fh_rx_cmd, timeout) != 0) //SDX55 need 4 seconds
    return -1;

  return 0;
}

static int fh_send_rawmode_image(struct fh_data *fh_data, const struct fh_cmd *fh_cmd, unsigned timeout)
{
    char full_path[512];
    char *unix_filename = strdup(fh_cmd->program.filename);
    char *ptmp;
    FILE *fp;
    size_t filesize, filesend;
    void *pbuf = malloc(fh_data->MaxPayloadSizeToTargetInBytes);

    if (pbuf == NULL)
        return -1;

    while((ptmp = strchr(unix_filename, '\\'))) {
        *ptmp = '/';
    }

    snprintf(full_path, sizeof(full_path), "%.255s/%.240s", fh_data->firehose_dir, unix_filename);
    fp = fopen(full_path, "rb");
    if (!fp) {
        printf("fail to fopen %s, errno: %d (%s)\n", full_path, errno, strerror(errno));
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    filesend = 0;
    fseek(fp, 0, SEEK_SET);

    while (filesend < filesize) {
      size_t reads;
      if (filesize < (filesend + fh_data->MaxPayloadSizeToTargetInBytes)) {
        reads = fread(pbuf, 1, filesize - filesend, fp);
      } else {
        reads = fread(pbuf, 1, fh_data->MaxPayloadSizeToTargetInBytes, fp);
      }

      if (reads <= 0) {
        break;
      }
      if (reads % fh_cmd->program.SECTOR_SIZE_IN_BYTES) {
        memset((uint8_t *)pbuf + reads, 0, fh_cmd->program.SECTOR_SIZE_IN_BYTES - (reads % fh_cmd->program.SECTOR_SIZE_IN_BYTES));
        reads +=  fh_cmd->program.SECTOR_SIZE_IN_BYTES - (reads % fh_cmd->program.SECTOR_SIZE_IN_BYTES);
      }
      size_t writes = usbfs_bulk_write(fh_data->usb_handle , pbuf , reads, timeout, 1);
      if (reads != writes) {
        printf("%s send fail reads=%zd, writes=%zd\n", __func__, reads, writes);
        printf("%s send fail filesend=%zd, filesize=%zd\n", __func__, filesend, filesize);
        break;
      }
      filesend += reads;
      printf(".");

    }

    fclose(fp);
    free(unix_filename);
    free(pbuf);

    printf("\n");
    if (filesend >= filesize) {
      printf("send finished\n");
      return 0;
    }

    printf("send finished unsuccesfully \n");
    return -1;
}



static int fh_process_program(struct fh_data *fh_data, struct fh_cmd *fh_cmd)
{
  struct fh_cmd fh_rx_cmd;

  fh_send_cmd(fh_data, fh_cmd);
  if (fh_wait_response_cmd(fh_data, &fh_rx_cmd, 3000) != 0) {
    printf("fh_wait_response_cmd fail\n");
    return -1;
  }
  if (strcmp(fh_rx_cmd.response.value, "ACK")) {
    printf("response should be ACK\n");
    return -1;
  }

  if (fh_rx_cmd.response.rawmode != 1) {
    printf("response should be rawmode true\n");
    return -1;
  }

  if (fh_send_rawmode_image(fh_data, fh_cmd, 15000)) {
    printf("fh_send_rawmode_image fail\n");
    return -1;
  }
  if (fh_wait_response_cmd(fh_data, &fh_rx_cmd, 6000) != 0) {
    printf("fh_wait_response_cmd fail\n");
    return -1;
  }
  if (strcmp(fh_rx_cmd.response.value, "ACK")) {
    printf("response should be ACK\n");
    return -1;
  }
  if (fh_rx_cmd.response.rawmode != 0) {
    printf("response should be rawmode false\n");
    return -1;
  }

  free(fh_cmd->program.filename);

  return 0;
}

static int fh_send_reset_cmd(struct fh_data *fh_data)
{
  struct fh_cmd fh_reset_cmd;
  fh_reset_cmd.cmd.type = "reset";
  return fh_send_cmd(fh_data, &fh_reset_cmd);
}

static int fh_parse_xml_file(struct fh_data *fh_data, const char *xml_file)
{
  FILE *fp = fopen(xml_file, "rb");
  
  if (fp == NULL) {
    printf("%s fail to fopen(%s), errno: %d (%s)\n", __func__, xml_file, errno, strerror(errno));
    return -1;
  }

  while (fgets(fh_data->xml_tx_buf, fh_data->xml_tx_size, fp)) {
    char *xml_line = strstr(fh_data->xml_tx_buf, "<");
    char *c_start = NULL;
    
    if (!xml_line)
      continue;
    c_start = strstr(xml_line, "<!--");
    if (c_start) {
      char *c_end = strstr(c_start, "-->");
      if (c_end) {
        /*
          <erase case 1 /> <!-- xxx -->
          <!-- xxx --> <erase case 2 />
          <!-- <erase case 3 /> -->
        */
        char *tmp = strstr(xml_line, "/>");
        if (tmp && (tmp < c_start || tmp > c_end)) {
          memset(c_start, ' ', c_end - c_start + strlen("-->"));
          goto __fh_parse_xml_line;
        }
        continue;
      }
      else {
        /*
          <!-- line1
          <! -- line2 -->
          -->
        */
        do {
          if (fgets(fh_data->xml_tx_buf, fh_data->xml_tx_size, fp) == NULL) {
            break;
          };
          xml_line = fh_data->xml_tx_buf;
        } while (!strstr(xml_line, "-->") && strstr(xml_line, "<!--"));

        continue;
      }
    }

  __fh_parse_xml_line:
    if (xml_line) {
      char *tag = NULL;
      tag = strstr(xml_line, "<erase ");
      if (!tag) {
        tag = strstr(xml_line, "<program ");
        if (!tag) {
          tag = strstr(xml_line, "<patch ");
        }
      }

      if (tag) {
        if (!fh_parse_xml_line(tag, &fh_data->fh_cmd_table[fh_data->fh_cmd_count])) {
          fh_data->fh_cmd_count++;
          if (strstr(tag, "<patch "))
            fh_data->fh_patch_count++;
        }
      }
      else if (!strstr(xml_line, "<?xml") && !strcmp(xml_line, "<data>") && !strcmp(xml_line, "</data>")
               && !strcmp(xml_line, "<patches>") && !strcmp(xml_line, "<patches>")) {
        printf("unspport xml_line '%s'\n", xml_line);
        exit(-1);
      }
    }
  }

  fclose(fp);

  return 0;
}

int firehose_main(const char *firehose_dir, struct qdl_device *qdl)
{

  char firehose_file[PATH_LENGTH];
  struct fh_cmd fh_rx_cmd;
  struct fh_data *fh_data;
  int i = 0;
  memset(firehose_file, 0 , PATH_LENGTH);

  fh_data  = (struct fh_data *)malloc(sizeof(struct fh_data));

  if (!fh_data) {
    return -1;
  }

  memset(fh_data, 0, sizeof(struct fh_data));

  fh_data->firehose_dir = firehose_dir;
  fh_data->usb_handle = qdl;
  fh_data->xml_tx_size = sizeof(fh_data->xml_tx_buf);
  fh_data->xml_rx_size = sizeof(fh_data->xml_rx_buf);
  fh_data->ZlpAwareHost = 1;

  snprintf(firehose_file, PATH_LENGTH, "%s/%s", firehose_dir, RAW_PROGRAM_FILE);
  printf("FIREHOSE: looking for the firehose file in : %s\n", firehose_file);
  fh_parse_xml_file(fh_data, firehose_file);

  // Before doing anything the commands need repairs
  // Sometimes the configuration does not match reality when is about file size

   usleep(300000);

   while( (fh_recv_cmd(fh_data, &fh_rx_cmd,-1) != -1) && (i < 1) ) {
       i++;
       usleep(100000);
  };

  printf("Start sending commands!\n");
  // Send configuration data
  if (fh_send_cfg_cmd(fh_data)) {
    printf("FIREHOSE configuration failed. Bailing out now \n");
    return -1;
  }

  //Apply all erase commands first
 
  for (unsigned int x = 0; x < fh_data->fh_cmd_count; x++) {
    struct fh_cmd *fh_cmd = &fh_data->fh_cmd_table[x];
    if (!strstr(fh_cmd->cmd.type, "erase"))
      continue;
    if (fh_cmd->erase.SECTOR_SIZE_IN_BYTES == 0) 
      continue;
    if (fh_process_erase(fh_data, fh_cmd)) {
      printf("FIREHOSE: cannot apply erase commands");
    } 
  }

  //Apply all programm commands

  for (unsigned int x = 0; x < fh_data->fh_cmd_count; x++) {
    struct fh_cmd *fh_cmd = &fh_data->fh_cmd_table[x];
    if (!strstr(fh_cmd->cmd.type, "program"))
      continue;
    if (fh_cmd->program.start_sector != 0)
      continue;
    if (fh_validate_program_cmd(fh_data, fh_cmd)!= 0) {
      printf("FIREHOSE: cannot flash this file\n");
      continue;
    } 
    fh_process_program(fh_data, fh_cmd);
  }

  // Job done reset the target now

  fh_send_reset_cmd(fh_data);
  if (fh_wait_response_cmd(fh_data, &fh_rx_cmd, 3000) != 0) {
    return -5;
  }
  return 0;
}
