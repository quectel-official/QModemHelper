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

#ifndef _QL_QDL_FIREHOSE_H_
#define _QL_QDL_FIREHOSE_H_
#include "ql-sahara-core.h"

#define RAW_PROGRAM_FILE "rawprogram_nand_p2K_b128K_recovery.xml"


#define SPARSE_HEADER_MAGIC 0xed26ff3a

typedef struct sparse_header
{
  uint32_t magic;         /* 0xed26ff3a */
  uint16_t major_version; /* (0x1) - reject images with higher major versions */
  uint16_t minor_version; /* (0x0) - allow images with higer minor versions */
  uint16_t file_hdr_sz;   /* 28 bytes for first revision of the file format */
  uint16_t chunk_hdr_sz;  /* 12 bytes for first revision of the file format */
  uint32_t blk_sz;       /* block size in bytes, must be a multiple of 4 (4096) */
  uint32_t total_blks;   /* total blocks in the non-sparse output image */
  uint32_t total_chunks; /* total chunks in the sparse input image */
  uint32_t image_checksum; /* CRC32 checksum of the original data, counting "don't care" */
  /* as 0. Standard 802.3 polynomial, use a Public Domain */
  /* table implementation */
} sparse_header_t;



typedef struct chunk_header {
  uint16_t chunk_type; /* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
  uint16_t reserved1;
  uint32_t chunk_sz; /* in blocks in output image */
  uint32_t total_sz; /* in bytes of chunk input file including chunk header and data */
} chunk_header_t;

typedef struct chunk_polymerization_params {
  uint32_t total_chunk_sz;
  uint32_t total_sz;
  uint16_t total_chunk_count;
  //uint16_t file_sector_offset;
}chunk_polymerization_param;

typedef struct SparseImgParams {
  chunk_polymerization_param chunk_polymerization_data[100];
  chunk_polymerization_param chunk_polymerization_cac3[100];
  uint16_t  total_count;
  uint16_t  total_cac3_count;
  uint16_t file_first_sector_offset;    
} SparseImgParam;

struct fh_configure_cmd {
    const char *type;
    const char *MemoryName;
    uint32_t Verbose;
    uint32_t AlwaysValidate;
    uint32_t MaxDigestTableSizeInBytes;
    uint32_t MaxPayloadSizeToTargetInBytes;
    uint32_t MaxPayloadSizeFromTargetInBytes ; 			//2048
    uint32_t MaxPayloadSizeToTargetInByteSupported;		//16k
    uint32_t ZlpAwareHost;
    uint32_t SkipStorageInit;
};

struct fh_erase_cmd {
    const char *type;
    //uint32_t PAGES_PER_BLOCK;
    uint32_t SECTOR_SIZE_IN_BYTES;
    //char label[32];
    uint32_t last_sector;
    uint32_t num_partition_sectors;
    //uint32_t physical_partition_number;
    uint32_t start_sector;
};

struct fh_program_cmd {
    const char *type;
    char *filename;
    char *sparse;
    uint32_t filesz;
    //uint32_t PAGES_PER_BLOCK;
    uint32_t SECTOR_SIZE_IN_BYTES;
    //char label[32];
    //uint32_t last_sector;
    uint32_t num_partition_sectors;
    uint32_t physical_partition_number;
    uint32_t start_sector;
    uint32_t file_sector_offset;
    uint32_t UNSPARSE_FILE_SIZE;
    //char sparse[16];
};

struct fh_response_cmd {
    const char *type;
    const char *value;
    uint32_t rawmode;
    uint32_t MaxPayloadSizeToTargetInBytes;
};

struct fh_log_cmd {
    const char *type;
};

struct fh_patch_cmd {
    const char *type;
    char *filename;
    uint32_t filesz;
    uint32_t SECTOR_SIZE_IN_BYTES;
    uint32_t num_partition_sectors;
};

struct fh_cmd_header {
    const char *type;
};

struct fh_vendor_defines {
    const char *type; // "vendor"
};

struct fh_cmd {
    union {
        struct fh_cmd_header cmd;
        struct fh_configure_cmd cfg;
        struct fh_erase_cmd erase;
        struct fh_program_cmd program;
        struct fh_response_cmd response;
        struct fh_log_cmd log;
        struct fh_patch_cmd patch;
        struct fh_vendor_defines vdef;
    };
    int part_upgrade;
    char xml_original_data[512];
};

struct fh_data {
    const char *firehose_dir;
    struct qdl_device* usb_handle;
    unsigned MaxPayloadSizeToTargetInBytes;
    unsigned fh_cmd_count;
    unsigned fh_patch_count;
    unsigned ZlpAwareHost;
    struct fh_cmd fh_cmd_table[256]; //AG525 have more than 64 partition
    unsigned xml_tx_size;
    unsigned xml_rx_size;
    char xml_tx_buf[1024];
    char xml_rx_buf[1024];
};


int firehose_main(const char *firehose_dir, struct qdl_device *qdl);

#endif
