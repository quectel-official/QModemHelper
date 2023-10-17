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


#include "ql-gpio.h"
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

const char kGpioSysFsPath[]  = "/sys/class/gpio";                                                                                                                                                                                    
const char kGpioExportPath[] = "/sys/class/gpio/export";                                                                                                                                                                            
const char kGpioPathPrefix[] = "/sys/class/gpio/gpio";  

const useconds_t kUdevWait   = 500 ;                                                                                                                                                                       
const useconds_t kToggleWait = 1000;                                                                                                                                                                        
                                                                     
int gpio_reboot_modem(int reset_line)
{
    DIR *dp;
    FILE * base_fp;
    FILE * export_fp;
    FILE* direction_fp;
    FILE* value_fp;
    struct dirent *dirp;
    int gpio_base_line = 0;
    char local_path[MAX_FILE_NAME_LEN]={0};
    char relative_gpio_line_path[MAX_FILE_NAME_LEN]={0};
    char absolute_gpio_line_path[MAX_FILE_NAME_LEN]={0};
    char gpio_line_direction[MAX_FILE_NAME_LEN]={0};
    char gpio_line_value[MAX_FILE_NAME_LEN]={0};
    int gpio_line_ready = 0;
    int line_retries = 100;
    int max_retry = 5;
    int ret;

    if ((dp = opendir(kGpioSysFsPath)) == NULL) {
        syslog(0, "can't open %s", kGpioSysFsPath);
        return EXIT_FAILURE;
    }


    while ((dirp = readdir(dp)) != NULL) {
        memset(local_path,0, MAX_FILE_NAME_LEN );
        strcat(local_path,kGpioSysFsPath);
        strcat(local_path,"/");
        strcat(local_path,dirp->d_name);
        if (strstr(local_path , kGpioPathPrefix)) {
            syslog(0, "Found a chip at : %s\n", local_path);
            strcat(local_path,"/base");
            base_fp = fopen (local_path, "r");
            if (!base_fp) {
                continue;
            }
            ret = fscanf(base_fp, "%d", &gpio_base_line);
            if (!ret) {
              syslog(0, "Cannot obtain the baseline address for the gpio chip");
              fclose(base_fp);
              closedir(dp);
              return EXIT_FAILURE;
            }
            fclose(base_fp);
            break;
        }
    }
    closedir(dp);

    //TODO::Must remove the above logic to find gpio base.
    snprintf (relative_gpio_line_path , MAX_FILE_NAME_LEN , "%d", reset_line );
    strcat(absolute_gpio_line_path,kGpioPathPrefix);
    strcat(absolute_gpio_line_path,relative_gpio_line_path);
    syslog(0, "Exporting gpio line: %s\n", absolute_gpio_line_path  );

    // TODO:: This is for testing to delay the attempt of setting the direction of
    // the GPIO line before udev sets the right permissions
    usleep(kUdevWait);
    // Check if gpio line is ready for input
    if ( (dp = opendir(absolute_gpio_line_path))) {
        syslog(0, "Gpio line ready\n");
        gpio_line_ready = 1;
        closedir(dp);
    } else {
        export_fp = fopen(kGpioExportPath,"w");
        if (!export_fp) {
            syslog(LOG_ERR, "Error opening GPIO line export file: %s", strerror(errno));
            return EXIT_FAILURE;
        }
        fprintf(export_fp,"%d", reset_line);
        fclose(export_fp);
    }

    // TODO: long sleep to give udev time to setup the permissions
    usleep(kUdevWait);
    while(!gpio_line_ready) {
        if (!(line_retries--)) {
            break;
        }
        if (!(dp = opendir(absolute_gpio_line_path))) {
         usleep(kUdevWait);
         break;   
        }
        
        syslog(0, "Gpio line is getting ready\n");
        gpio_line_ready = 1;
        closedir(dp);
    }

    if (!gpio_line_ready) {
        syslog(0, "Can't get the gpio line ready\n");
        return EXIT_FAILURE;
    }
    
    // Seems that line is ready let's set it to direction out
    strcat(gpio_line_direction,absolute_gpio_line_path);
    strcat(gpio_line_direction,"/direction");
    
    while(max_retry > 0){
        sleep(1);
        direction_fp = fopen(gpio_line_direction,"w+");
        if(direction_fp)
            break;
        syslog(LOG_ERR, "Error opening GPIO line direction file: %s, try after 1 second.", strerror(errno));
        max_retry--;
        
    }

    if(!direction_fp) {
        syslog(LOG_ERR, "Failed to open GPIO line direction file");
        return EXIT_FAILURE;
    } else {
        fprintf(direction_fp, "out");
        fclose(direction_fp);
    }

    // Gpio line direction is set to out now let's flip it
    strcat(gpio_line_value, absolute_gpio_line_path);
    strcat(gpio_line_value, "/value");
    value_fp = fopen(gpio_line_value,"w+");
    if (!value_fp) {
        syslog(LOG_ERR, "Error opening GPIO line value file: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    fprintf(value_fp, "0");
    fclose(value_fp);

    sleep(1);
    value_fp = fopen(gpio_line_value,"w+");
    if (!value_fp) {
        syslog(LOG_ERR, "Error opening GPIO line value file: %s", strerror(errno));
        return EXIT_FAILURE;
    }
    fprintf(value_fp, "1");
    fclose(value_fp);

   return EXIT_SUCCESS;
}
