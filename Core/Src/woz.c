

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fatfs.h"
#include "fatfs_sdcard.h"

#include "woz.h"
#include "main.h"

#define WOZ_ERR_FILE_NOT_FOUND -1;
__uint8_t TMAP[160];
__uint16_t BLK_startingBlocOffset[160];

unsigned int fatClusterWOZ[20];

extern unsigned char mountedImageFile;

int mountWozFile(char * filename){
    
    FRESULT fres; 
    FIL fil;  
    unsigned long t1,t2,diff;
    fres = f_open(&fil,filename , FA_READ);    

    if(fres != FR_OK){
        printf("File open Error: (%i)\r\n", fres);
        mountedImageFile=0;
        return -1;
    } 

    long clusty=fil.obj.sclust;
    int i=0;
    fatClusterWOZ[i]=clusty;
    printf("file cluster %d:%ld\n",i,clusty);
  
    while (clusty!=1 && i<30){
        i++;
        clusty=get_fat((FFOBJID*)&fil,clusty);
        //printf("file cluster WOZ %d:%ld\n",i,clusty);
        fatClusterWOZ[i]=clusty;
    }


    // Start reading TMAP
    unsigned int pt;
    char * tmp=(char *)malloc(160*sizeof(char));
    f_lseek(&fil,88);
    f_read(&fil,tmp,160,&pt);
    
    for (int i=0;i<160;i++){
        TMAP[i]=tmp[i];
        //printf("debug tmap %03d: %02d\n",i,TMAP[i]);
    }
    free(tmp);
    f_lseek(&fil,256);
    
    tmp=(char *)malloc(160*8*sizeof(char));
    f_read(&fil,tmp,160*8,&pt);
    for (int i=0;i<160;i++){
        BLK_startingBlocOffset[i]=(((unsigned short)tmp[i*8+1] << 8) & 0xF00) | tmp[i*8];
       // printf("debug blk starting bloc %03d: %02d\n",i,BLK_startingBlocOffset[i]);
    }

    free(tmp);
    mountedImageFile=1;
    /*
    f_lseek(&fil,1536);
    tmp=(char *)malloc(1024*sizeof(char));
    DWT->CYCCNT = 0;                                                                  // Reset cpu cycle counter
    t1 = DWT->CYCCNT;
    f_read(&fil,tmp,1024,&pt);
    t2 = DWT->CYCCNT;
    diff = t2 - t1;
    printf("timelapse fread %ld cycles\n",diff);
    
    dumpBuf(tmp,2,1024);
    */
    
    f_close(&fil);

return 0;
}
