

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fatfs.h"
#include "fatfs_sdcard.h"

#include "woz.h"

#define WOZ_ERR_FILE_NOT_FOUND -1;
__uint8_t TMAP[160];
__uint16_t BLK_startingBlocOffset[160];

unsigned int fatClusterWOZ[20];

int mountWozFile(char * filename){
    
    FRESULT fres; 
    FIL fil;  

    fres = f_open(&fil,filename , FA_READ);     // Step 2 Open the file long naming

    if(fres != FR_OK){
        printf("File open Error: (%i)\r\n", fres);
        return -1;
    } 

    long clusty=fil.obj.sclust;
    int i=0;
    fatClusterWOZ[i]=clusty;
    printf("file cluster %d:%ld\n",i,clusty);
  
    while (clusty!=1 && i<30){
        i++;
        clusty=get_fat((FFOBJID*)&fil,clusty);
        printf("file cluster WOZ %d:%ld\n",i,clusty);
        fatClusterWOZ[i]=clusty;
    }


    // Start reading TMAP
    unsigned int pt;
    char * tmp=(char *)malloc(160*sizeof(char));
    f_lseek(&fil,88);
    f_read(&fil,tmp,160,&pt);
    
    for (int i=0;i<160;i++){
        TMAP[i]=tmp[i];
        printf("debug tmap %03d: %02d\n",i,TMAP[i]);
    }
    free(tmp);
    f_lseek(&fil,256);
    
    tmp=(char *)malloc(160*8*sizeof(char));
    f_read(&fil,tmp,160*8,&pt);
    for (int i=0;i<160;i++){
        BLK_startingBlocOffset[i]=(((unsigned short)tmp[i*8+1] << 8) & 0xF00) | tmp[i*8];
        printf("debug blk starting bloc %03d: %02d\n",i,BLK_startingBlocOffset[i]);
    }

    free(tmp);



    
    f_close(&fil);

return 0;
}
