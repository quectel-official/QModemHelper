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

#include "ql-qdl-sahara.h"
#include "ql-qdl-firehose.h"
#include <stdio.h>
#include <libgen.h>

void sahara_hello(struct qdl_device *qdl, struct sahara_pkt *pkt)
{
  struct sahara_pkt resp;

  assert(pkt->length == 0x30);
  resp.cmd = 2;
  resp.length = 0x30;
  resp.hello_resp.version = 2;
  resp.hello_resp.compatible = pkt->hello_req.compatible;
  resp.hello_resp.status = 0;
  resp.hello_resp.mode = 0; 
  printf("SENDING --> SAHARA_HELLO_RESPONSE\n");
  qdl_write(qdl, &resp, resp.length);
  return;
}

int sahara_done(struct qdl_device *qdl) {

  struct sahara_pkt resp;

  resp.cmd = 5;
  resp.length = sizeof(struct sahara_pkt);
  resp.length = 0x08;

  printf("SENDING --> SAHARA_DONE\n");
  qdl_write(qdl, &resp, resp.length);
  return 0;
}

int start_program_transfer(struct qdl_device *qdl ,  struct sahara_pkt *pkt ,FILE *file_handle)
{
  int retval = 0;
  uint32_t nBytesRead = 0;
  uint32_t nBytesToRead;
  uint32_t readOffset = le_uint32(pkt->read_req.offset);
  uint32_t readLen = le_uint32(pkt->read_req.length);
  void* tx_buffer = (void*)malloc(QBUFFER_SIZE);

  memset(tx_buffer, 0, QBUFFER_SIZE);
  printf("%s: Image id: 0x%08x offset: 0x%08x length :0x%08x\n", __FUNCTION__ ,
         le_uint32(pkt->read_req.image),
         readOffset,
         readLen);

  if (fseek(file_handle, (long)readOffset, SEEK_SET)) {
    printf("%d errno: %d (%s)\n", __LINE__, errno, strerror(errno));
    return -1;
  }

  while(nBytesRead < readLen) {
    nBytesToRead = MIN((uint32_t)readLen - nBytesRead, QBUFFER_SIZE);
    retval = fread(tx_buffer, 1, nBytesToRead, file_handle);
    if (retval < 0) {
      printf("file read failed: %s\n", strerror(errno));
      return -2;
    }
    // Transmit the data
    if (0 == qdl_write(qdl, tx_buffer, nBytesToRead)) {
      dbg("Tx Sahara Image Failed\n");
      return 0;
    }
    nBytesRead += nBytesToRead;

  }
  return 0;
}

int qdl_flash_all(char * main_file_path,char*  oem_file_path,char* carrier_file_path)
{
  struct qdl_device qdl;
  int ret;
  struct sahara_pkt *pspkt;
  char buffer[QBUFFER_SIZE];
  int nBytes = 0;
  int done = 0;
  char full_programmer_path[PATH_LENGTH];
  FILE* file_handle = NULL;

  memset(full_programmer_path, 0,PATH_LENGTH);

  if (main_file_path) {
    printf("main: %s\n", main_file_path);
  }
  if (oem_file_path) {
    printf("oem: %s\n", oem_file_path);

    sprintf(full_programmer_path , "%s/%s", dirname(oem_file_path), "prog_nand_firehose_9x55.mbn" );
    printf("programmer path : %s\n", full_programmer_path);
    file_handle = fopen(full_programmer_path, "rb");
    if (file_handle == NULL) {
      printf("%s %d %s errno: %d (%s)", __func__, __LINE__, full_programmer_path, errno, strerror(errno));
      return ENOENT;
    }
  }

  if (carrier_file_path) {
    printf("oem: %s\n", carrier_file_path);
  }

  ret = qdl_open(&qdl);

  if (ret == SWITCHED_TO_EDL) {
    printf("%s : the device is EDL mode \n",__FUNCTION__);
  }
  
  memset(buffer, 0 , QBUFFER_SIZE );
  nBytes = sahara_rx_data(&qdl, buffer, 0);
  pspkt = (struct sahara_pkt *)buffer;

  if (le_uint32(pspkt->cmd) != 0x01) {
    printf("Received a different command: %x while waiting for hello packet \n Bytes received %d\n", pspkt->cmd, nBytes);
    qdl_close(&qdl);
    return -1;
  }

  sahara_hello(&qdl, pspkt);
  while (!done) {
    memset(buffer, 0 , QBUFFER_SIZE );
    nBytes = sahara_rx_data(&qdl, buffer, 0);
    pspkt = (struct sahara_pkt *)buffer;
    if (nBytes == 0) {
      printf("no bytes were red \n");
      break;
    }

    switch(le_uint32(pspkt->cmd)) {
    case 0x03:
      start_program_transfer(&qdl, pspkt, file_handle);
      break;
    case 0x04:
      printf("Finishing for imaged id: %d with status: %d\n",
             le_uint32(pspkt->eoi.image), le_uint32(pspkt->eoi.status));
      if ( le_uint32(pspkt->eoi.status) == 0) {
        sahara_done(&qdl);
        printf("STATE <-- WAITING TO FINISH\n");
      }
      break;
    case 0x06:
      printf("Sahara Done Response status %d\n", le_uint32(pspkt->done_resp.status));
      done = 1; // yeap we are done. done;
    default:
      break;
    }
  }

  firehose_main(oem_file_path,&qdl);

  qdl_close(&qdl);
  return 0;
}
