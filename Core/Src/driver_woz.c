#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fatfs.h"
#include "fatfs_sdcard.h"

#include "driver_woz.h"
#include "main.h"
#include "log.h"

__uint8_t TMAP[160];
__uint16_t BLK_startingBlocOffset[160];
woz_info_t wozFile;

const char logPrefix[]="[woz_driver]";

extern long database;                                            // start of the data segment in FAT
extern int csize;  
unsigned int fatWozCluster[20];
char * woz1_256B_prologue;                                       // needed to store the potential overwrite 

int getWozTrackFromPh(int phtrack){
   return TMAP[phtrack];
}

long getSDAddrWoz(int trk,int block,int csize, long database){
  long rSector=-1;
  if (wozFile.version==2){
    int long_sector = BLK_startingBlocOffset[trk] + block;
    int long_cluster = long_sector >> 6;
    int ft = fatWozCluster[long_cluster];
    rSector=database+(ft-2)*csize+(long_sector & (csize-1));
  }else if (wozFile.version==1){
    int long_sector = 13*trk;                                // 13 block of 512 per track
    int long_cluster = long_sector >> 6;
    int ft = fatWozCluster[long_cluster];
    rSector=database+(ft-2)*csize+(long_sector & (csize-1));
  }
  
  return rSector;
}

enum STATUS getWozTrackBitStream(int trk,unsigned char * buffer){
  const unsigned int blockNumber=13; 
  int addr=getSDAddrWoz(trk,0,csize,database);
  
  if (addr==-1){
    printf("%s:Error getting SDCard Address for woz\n",logPrefix);
    return RET_ERR;
  }
  
  if (wozFile.version==2){
    cmd18GetDataBlocksBareMetal(addr,buffer,blockNumber);
  }else if (wozFile.version==1){
    unsigned char * tmp2=(unsigned char*)malloc((blockNumber+1)*512*sizeof(char));
    if (tmp2==NULL){
      printf("%s:Error memory alloaction getNicTrackBitStream: tmp2:8192 Bytes",logPrefix);
      return RET_ERR;
    }

    cmd18GetDataBlocksBareMetal(addr,tmp2,blockNumber+1);
    memcpy(buffer,tmp2+256,blockNumber*512-10);                                       // Last 10 Bytes are not Data Stream Bytes
    woz1_256B_prologue=malloc(256*sizeof(char));
    memcpy(woz1_256B_prologue,tmp2,256);                                              // we need this to speed up the write process
    free(tmp2);
  }
        
  return 1;
}

enum STATUS setWozTrackBitStream(int trk,unsigned char * buffer){
  const unsigned int blockNumber=13; 
  int addr=getSDAddrWoz(trk,0,csize,database);
  
  if (addr==-1){
    printf("%s:Error getting SDCard Address for woz\n",logPrefix);
    return RET_ERR;
  }
  
  if (wozFile.version==2){
    writeDataBlocks(addr,buffer,blockNumber);
    //cmd25SetDataBlocksBareMetal(addr,buffer,blockNumber);
  }else if (wozFile.version==1){
    
    unsigned char * tmp2=(unsigned char*)malloc(14*512*sizeof(char));
    if (tmp2==NULL){
      printf("%s:Error memory alloaction getNicTrackBitStream: tmp2:8192 Bytes",logPrefix);
      return RET_ERR;
    }

    // First we need to get the first 256 bytes of t
    memcpy(tmp2,woz1_256B_prologue,256);
    memcpy(tmp2+256,buffer,blockNumber*512);   
    cmd25SetDataBlocksBareMetal(addr,tmp2,blockNumber+1);               // <!> Last 10 Bytes are not Data Stream Bytes
    free(tmp2);
  }
        
  return 1;
}

enum STATUS mountWozFile(char * filename){
    
    FRESULT fres; 
    FIL fil;  

    fres = f_open(&fil,filename , FA_READ);    
    if(fres != FR_OK){
        printf("%s:File open Error: (%i)\r\n", logPrefix,fres);
        return RET_ERR;
    } 

    long clusty=fil.obj.sclust;
    int i=0;
    fatWozCluster[i]=clusty;
    printf("%s:file cluster %d:%ld\n",logPrefix,i,clusty);
  
    while (clusty!=1 && i<30){
        i++;
        clusty=get_fat((FFOBJID*)&fil,clusty);
        fatWozCluster[i]=clusty;
    }

    unsigned int pt;
    char * woz_header=(char*)malloc(4*sizeof(char));
    f_read(&fil,woz_header,4,&pt);
    if (!memcmp(woz_header,"\x57\x4F\x5A\x31",4)){               //57 4F 5A 31
        printf("Image:woz version 1\n");
        wozFile.version=1;
    }else if (!memcmp(woz_header,"\x57\x4F\x5A\x32",4)){
        printf("Image:woz version 2\n");
        wozFile.version=2;
    }else{
        printf("Error: not a woz file\n");
        return RET_ERR;
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
        printf("woz:Error Info Chunk is not valid\n");
        return RET_ERR;
    }
    free(info_chunk);

    // Start reading TMAP
    
    char * tmap_chunk=(char *)malloc(168*sizeof(char));
    if (!tmap_chunk)
        return RET_ERR;

    f_lseek(&fil,80);
    f_read(&fil,tmap_chunk,168,&pt);
    
    if (!memcmp(tmap_chunk,"\x54\x4D\x41\x50",4)){          // 0x50414D54          
        for (int i=0;i<160;i++){
            TMAP[i]=tmap_chunk[i+8];
            //printf("debug tmap %03d: %02d\n",i,TMAP[i]);
        }
    }else{
        printf("Error tmp Chunk is not valid\n");
        free(tmap_chunk);
        return RET_ERR;
    }
    
    free(tmap_chunk);

    if (wozFile.version==2){
        char * trk_chunk=(char *)malloc(1280*sizeof(char));
        if (!trk_chunk)
            return RET_ERR;
        
        f_lseek(&fil,248);
        f_read(&fil,trk_chunk,1280,&pt);

        if (!memcmp(trk_chunk,"\x54\x52\x4B\x53",4)){          // 0x534B5254          // ERREUR A FIXER ICI

            for (int i=0;i<160;i++){
                BLK_startingBlocOffset[i]=(((unsigned short)trk_chunk[i*8+8+1] << 8) & 0xF00) | trk_chunk[i*8+8];
                //printf("debug blk starting bloc %03d: %02d\n",i,BLK_startingBlocOffset[i]);
            }
        }else{
            printf("Error trk Chunk is not valid\n");
            free(trk_chunk);
            return RET_ERR;
        }

        free(trk_chunk);
    }else{
        printf("woz file type 1 no trk_chunk\n");
        //return RET_OK;
    }

    f_close(&fil);
    return RET_OK;
}
