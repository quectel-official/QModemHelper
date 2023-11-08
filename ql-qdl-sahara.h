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
#ifndef __QL_QDL_SAHARA_H_
#define __QL_QDL_SAHARA_H_
#include "ql-sahara-core.h"

void sahara_hello(struct qdl_device *qdl, struct sahara_pkt *pkt);
int sahara_done(struct qdl_device *qdl);
int qdl_flash_all(char * main_file_path,char*  oem_file_path,char* carrier_file_path);
int start_program_transfer(struct qdl_device *qdl ,  struct sahara_pkt *pkt ,FILE *file_handle);

#endif
