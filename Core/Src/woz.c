

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
woz_info_t wozFile;
unsigned int fatCluster[20];

extern unsigned char mountedImageFile;

int mountWozFile(char * filename){
    
    FRESULT fres; 
    FIL fil;  
    //unsigned long t1,t2,diff;
    fres = f_open(&fil,filename , FA_READ);    

    if(fres != FR_OK){
        printf("File open Error: (%i)\r\n", fres);
        mountedImageFile=0;
        return -1;
    } 

    long clusty=fil.obj.sclust;
    int i=0;
    fatCluster[i]=clusty;
    printf("file cluster %d:%ld\n",i,clusty);
  
    while (clusty!=1 && i<30){
        i++;
        clusty=get_fat((FFOBJID*)&fil,clusty);
        //printf("file cluster WOZ %d:%ld\n",i,clusty);
        fatCluster[i]=clusty;
    }

    unsigned int pt;
    char * woz_header=(char*)malloc(4*sizeof(char));
    f_read(&fil,woz_header,4,&pt);
    if (!memcmp(woz_header,"\x57\x4F\x5A\x31",4)){               //57 4F 5A 31
        printf("woz version 1\n");
        wozFile.version=1;
    }else if (!memcmp(woz_header,"\x57\x4F\x5A\x32",4)){
        printf("woz version 2\n");
        wozFile.version=2;
    }else{
        printf("Error: not a woz file\n");
    }
    free(woz_header);

    // Getting the Info Chunk
    char *info_chunk=(char*)malloc(60*sizeof(char));
    f_lseek(&fil,12);
    f_read(&fil,info_chunk,60,&pt);

    if (!memcmp(info_chunk,"\x49\x4E\x46\x4F",4)){                  // Little Indian 0x4F464E49  
        
        wozFile.disk_type=(int)info_chunk[1];
        wozFile.is_write_protected=(int)info_chunk[2];
        wozFile.sync=(int)info_chunk[3];
        wozFile.cleaned=(int)info_chunk[4];
        memcpy(wozFile.creator,info_chunk+5,32);
        
    }else{
        printf("Error Info Chunk is not valid\n");
    }


    // Start reading TMAP
    
    char * tmap_chunk=(char *)malloc(168*sizeof(char));
    f_lseek(&fil,80);
    f_read(&fil,tmap_chunk,168,&pt);
    
    if (!memcmp(tmap_chunk,"\x54\x4D\x41\x50",4)){          // 0x50414D54          

        for (int i=0;i<160;i++){
            TMAP[i]=tmap_chunk[i+8];
            //printf("debug tmap %03d: %02d\n",i,TMAP[i]);
        }
    }else{
        printf("Error tmp Chunk is not valid\n");
    }
    
    free(tmap_chunk);

    
    if (wozFile.version==2){
        char * trk_chunk=(char *)malloc(1280*sizeof(char));
        
        f_lseek(&fil,248);
        f_read(&fil,trk_chunk,1280,&pt);

        if (!memcmp(trk_chunk,"\x54\x52\x4B\x53",4)){          // 0x534B5254          

            for (int i=0;i<160;i++){
                BLK_startingBlocOffset[i]=(((unsigned short)trk_chunk[i*8+8+1] << 8) & 0xF00) | trk_chunk[i*8+8];
                //printf("debug blk starting bloc %03d: %02d\n",i,BLK_startingBlocOffset[i]);
            }
        }else{
            printf("Error trk Chunk is not valid\n");
        }

        free(trk_chunk);
    }else{
        printf("woz file type 1 no trk_chunk\n");
    }

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
