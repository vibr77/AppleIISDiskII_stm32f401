/* USER CODE BEGIN Header */
/*
__   _____ ___ ___        Author: Vincent BESSON
 \ \ / /_ _| _ ) _ \      Release: 0.1
  \ V / | || _ \   /      Date: 2024
   \_/ |___|___/_|_\      Description: Apple Disk II Emulator on STM32F4x
                2024      Licence: Creative Commons
______________________

Note 
+ CubeMX is needed to generate the code not included such as drivers folders and ...
+ An update of fatfs is required to manage long filename otherwise it crashes, v0.15 
+ any stm32fx would work, only available ram size is important >= 32 kbytes to manage 3 full track in // and also write buffer

Lessons learne:
- SDCard CMD17 is not fast enough due to wait before accessing to the bloc (140 CPU Cycle at 64 MHz), prefer CMD18 with multiple blocs read,
- Circular buffer with partial track is possible but needs complex coding to manage buffer copy and correct SDcard timing (I do not recommand),
- bitstream output is made via DMA SPI (best accurate option), do not use baremetal bitbanging with assemnbly (my first attempt) it is not accurate in ARM with internal interrupt,
- Use Interrupt for head move on Rising & Falling Edge => Capturing 1/4 moves

Current status: READ PARTIALLY WORKING / WRITE NOT YET
+ woz file support : in progress first images are working
+ NIC file support : in progress first images are working

Architecture:
- TIM1 Timer 1 is used generate a PWM for the logic analyzer to decode NIB (will be removed once stabilized)
- TIM2 Timer 2 is used to debounce sw button
- TIM3 Timer 3 is used for clock SP1 (RDData output Apple II)
- TIM4 Timer 4 is used for clock the counter on the WRDDATA side

- SPI1 is slave DMA transmit only (RDDATA)
- SPI2 is master full duplex to read / write SDCARD
- SPI3 is slave DMA receive only (WRDATA)

GPIO
BTN
- PC13 BTN_ENTR
- PC14 BTN_UP
- PC15 BTN_DOWN
- PB10 BTN_RET

STEP
- PA0 STEP0
- PA1 STEP1
- PA2 STEP2
- PA3 STEP3
SPI: 
- PA5 SPI1_SCK
- PA6 SPI1_MISO
- PB3 SPI3_SCK
- PB5 SPI3_MOSI
- PB12 SPI2_NSS
- PB13 SPI2_SCK
- PB14 SP2_MISO
- PB15 SP2_MOSI

- PA11 WR_REQ
- PA12 WR_PROTECT
- PA4 DEVICE_ENABLE
- PA7 SD_EJECT 

*/ 

/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "display.h"
#include "fatfs_sdcard.h"
#include "list.h"
#include "driver_woz.h"
#include "driver_nic.h"
#include "configFile.h"
#include "log.h"

//#include "parson.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
SPI_HandleTypeDef hspi3;
DMA_HandleTypeDef hdma_spi1_tx;
DMA_HandleTypeDef hdma_spi3_rx;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
volatile unsigned int *DWT_CYCCNT   = (volatile unsigned int *)0xE0001004;
volatile unsigned int *DWT_CONTROL  = (volatile unsigned int *)0xE0001000;
volatile unsigned int *DWT_LAR      = (volatile unsigned int *)0xE0001FB0;
volatile unsigned int *SCB_DHCSR    = (volatile unsigned int *)0xE000EDF0;
volatile unsigned int *SCB_DEMCR    = (volatile unsigned int *)0xE000EDFC;
volatile unsigned int *ITM_TER      = (volatile unsigned int *)0xE0000E00;
volatile unsigned int *ITM_TCR      = (volatile unsigned int *)0xE0000E80;
static int Debug_ITMDebug = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI2_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_SPI3_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */

bool buttonDebounceState=true;

volatile int ph_track=0;                                    // SDISK Physical track 0 - 139
volatile int intTrk=0;                                      // InterruptTrk                                    
unsigned char prevTrk=0;                                    // prevTrk to keep track of the last head track

unsigned int DMABlockSize=6464;//6592; // 6656              // Size of the DMA Buffer => full track width with 13 block of 512
unsigned int RawSDTrackSize=6656;                           // Maximuum track size on NIC & WOZ to load from SD
unsigned char read_track_data_bloc[19968];                  // 3 adjacent track 3 x RawSDTrackSize
  
unsigned char DMA_BIT_TX_BUFFER[6656];                      // DMA Buffer from the SPI
unsigned char DMA_BIT_RX_BUFFER[6656];                      // DMA Buffer from the SPI
unsigned char dump_rx_buffer[6656]; 
unsigned int woz_block_sel_012=0;                           // current index of the current track related to woz_track_data_bloc[3][6656];
int woz_sel_trk[3];                                         // keep track number in the selctor

volatile unsigned int WR_REQ_PHASE=0;
                              
long database=0;                                            // start of the data segment in FAT
int csize=0;                                                // Cluster size
extern uint8_t CardType;                                            // fatfs_sdcard.c type of SD card

volatile unsigned char flgDeviceEnable=0;
unsigned char flgImageMounted=0;                            // Image file mount status flag
unsigned char flgBeaming=0;                                 // DMA SPI1 to Apple II Databeaming status flag
volatile unsigned int  flgwhiteNoise=0;                              // White noise in case of blank 255 track to generate random bit 

enum STATUS (*getTrackBitStream)(int,unsigned char*);       // pointer to readBitStream function according to driver woz/nic
enum STATUS (*setTrackBitStream)(int,unsigned char*);       // pointer to writeBitStream function according to driver woz/nic
long (*getSDAddr)(int ,int ,int , long);                    // pointer to getSDAddr function
int  (*getTrackFromPh)(int);                                // pointer to track calculation function

enum page currentPage=0;
enum action nextAction=NONE;

void (*ptrbtnUp)(void *);                                   // function pointer to manage Button Interupt according to the page
void (*ptrbtnDown)(void *);
void (*ptrbtnEntr)(void *);
void (*ptrbtnRet)(void *);

volatile uint16_t rx_start_indx;
volatile uint16_t rx_end_indx;
volatile uint16_t tx_start_indx;
volatile uint16_t tx_end_indx;
volatile int tx_rx_indx_gap;
int p=0;

extern JSON_Object *configParams;

char selItem[256];
char currentFullPath[1024];                                     // Path from root

int lastlistPos;
list_t * dirChainedList;

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void EnableTiming(void){
  if ((*SCB_DHCSR & 1) && (*ITM_TER & 1)) // Enabled?
    Debug_ITMDebug = 1;
 
  *SCB_DEMCR |= 0x01000000;
  *DWT_LAR = 0xC5ACCE55;            // enable access
  *DWT_CYCCNT = 0;                  // reset the counter
  *DWT_CONTROL |= 1 ;               // enable the counter
}

enum STATUS dumpBufFile(char * filename,char * buffer,int length){

  FATFS FatFs; 	//Fatfs handle
  FIL fil; 		  //File handle
  FRESULT fres; //Result after operations
  
  fres = f_mount(&FatFs, "", 1); //1=mount now
  if (fres != FR_OK) {
	  printf("f_mount error (%i)\n", fres);
    return RET_ERR;
  }
  
  fres = f_open(&fil, filename, FA_WRITE | FA_OPEN_ALWAYS | FA_CREATE_ALWAYS);
  if(fres != FR_OK) {
	  printf("f_open error (%i)\n", fres);
    return RET_ERR;
  }
 
  UINT bytesWrote;
  UINT totalBytes=0;

  for (int i=0;i<13;i++){
    fres = f_write(&fil, buffer+i*512, 512, &bytesWrote);
    if(fres == FR_OK) {
      totalBytes+=bytesWrote;
    }else{
	    printf("f_write error (%i)\n",fres);
      return RET_ERR;
    }
  }
  printf("Wrote %i bytes to '%s'!\n", totalBytes,filename);
  f_close(&fil);
  return RET_OK;
}

enum STATUS writeTrkFile(char * filename,char * buffer,uint32_t offset){
  
  FATFS FatFs; 	//Fatfs handle
  FIL fil; 		  //File handle
  FRESULT fres; //Result after operations
  
  fres = f_mount(&FatFs, "", 1); //1=mount now
  if (fres != FR_OK) {
	  printf("f_mount error (%i)\n", fres);
    return RET_ERR;
  }
  
  fres = f_open(&fil, filename, FA_WRITE | FA_OPEN_ALWAYS | FA_CREATE_ALWAYS);
  if(fres != FR_OK) {
	  printf("f_open error (%i)\n", fres);
    return RET_ERR;
  }

  fres=f_lseek(&fil,offset);
  if(fres != FR_OK) {
    printf("f_lseek error");
    return RET_ERR;
  }

  UINT bytesWrote;
  UINT totalBytes=0;

  int blk=(DMABlockSize/512);
  int lst_blk_size=DMABlockSize%512;

  for (int i=0;i<blk;i++){
    fres = f_write(&fil, buffer+i*512, 512, &bytesWrote);
    if(fres == FR_OK) {
      totalBytes+=bytesWrote;
    }else{
	    printf("f_write error (%i)\n",fres);
      return RET_ERR;
    }
  }
  if (lst_blk_size!=0){
    fres = f_write(&fil, buffer+blk*512, lst_blk_size, &bytesWrote);
    if(fres == FR_OK) {
      totalBytes+=bytesWrote;
    }else{
      printf("f_write error (%i)\n",fres);
      return RET_ERR;
    }
  }

  printf("Wrote %i bytes to '%s' starting at %ld!\n", totalBytes,filename,offset);
  f_close(&fil);
  return RET_OK;

}

void dumpBuf(unsigned char * buf,long memoryAddr,int len){
  
  printf("Dump buffer:%ld\n",memoryAddr);
  for (int i=0;i<len;i++){
      if (i%16==0){
        if (i%512==0)
          printf("\n-");
        printf("\n%03X: ",i);
        
      }
    printf("%02X ",buf[i]);
   
  }
  printf("\n");
}

char *byte_to_binary(int x){
    char * b=(char*)malloc(9*sizeof(char));
    b[0] = '\0';

    int z;
    for (z = 128; z > 0; z >>= 1){
        strcat(b, ((x & z) == z) ? "1" : "0");
    }
    return b;
}

enum STATUS cmd17GetDataBlockBareMetal(long memoryAdr,unsigned char *buffer){
  int ret=0;
  if ((ret=getSDCMD(CMD17, memoryAdr) == 0))
    getSDDataBlockBareMetal((BYTE *)buffer, 512); // SPI command to read a block
  return RET_OK;
}

enum STATUS cmd18GetDataBlocksBareMetal(long memoryAdr,unsigned char * buffer,int count){
  
  int ret=0;
  if ((ret=getSDCMD(CMD18, memoryAdr) == 0)){
    do{
      if (!getSDDataBlockBareMetal((BYTE *)buffer, 512)){
        printf("SDCard getSDDataBlockBareMetal Error\n");
        ret=-1;
        break;
      }
        
        buffer += 512;
    } while (--count);
      /* STOP_TRANSMISSION */
      getSDCMD(CMD12, 0);
      if (ret==-1){
        return RET_ERR;
      }
  }
  else{
    printf("SDCard cmd18 Error\n");
    return RET_ERR;
  }
  return RET_OK;
}


enum STATUS cmd25SetDataBlocksBareMetal(long memoryAdr,unsigned char * buffer,int count){
  
  int ret=0;
  if (CardType & CT_SD1)
    {
      if ((ret=getSDCMD(CMD55, 0)!=0)){
          printf("SDCard cmd55 Error\n");
          return RET_ERR;
      }

      if ((ret=getSDCMD(CMD23, count)!=0)){
          printf("SDCard cmd23 Error\n");
          return RET_ERR;
      }
  }

  if ((ret=getSDCMD(CMD25, memoryAdr) == 0)){
    do{
      if (!setSDDataBlockBareMetal((BYTE *)buffer, 0xFC)){
        printf("SDCard setSDDataBlockBareMetal Error OxFC\n");
        ret=-1;
        break;
      }  
      buffer += 512;
    } while (--count);

      if(!setSDDataBlockBareMetal(0, 0xFD)){
        printf("SDCard setSDDataBlockBareMetal Error OxFD\n");
        ret=-1;
      }

      if (ret==-1){
        return RET_ERR;
      }

  }
  else{
    printf("SDCard cmd25 Error\n");
    return RET_ERR;
  }
  return RET_OK;
}

/*
 The next 2 functions are called during the DMA transfer from Memory to SPI1
 2 buffers are used to managed the timing of getting the data from the SDCard & populating the buffer
 At half of the transsfer, a new sector is requested via "prepareNewSector=1"
 At the end of populating the buffer, a switch is made to the other buffer
*/

void HAL_SPI_TxHalfCpltCallback(SPI_HandleTypeDef *hspi){
  
  if (flgwhiteNoise==1){                                // Enable non repeatable white noise on the first half track
    for (int i=0;i<DMABlockSize/2;i++){
          DMA_BIT_TX_BUFFER[i]=rand();
    }
  }
  return;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi){

  if (flgwhiteNoise==1){                                // Enable non repeatable white noise on the second half track
    for (int i=DMABlockSize/2;i<DMABlockSize;i++){   
          DMA_BIT_TX_BUFFER[i]=rand();
    }
  }
  return;
}

volatile unsigned int memcp_indx=0;
volatile unsigned int dst_s_index=0;
 int memcp_sub_indx[10];
 int memcp_op_src_s[10];
 int memcp_op_src_e[10];
 int memcp_op_dst_s[10];
 int memcp_op_dst_e[10];
 int memcp_op_len[10];
 int memcp_half[10];
 int total_byte_written=0;

 int memcp_op_sub_src_s[10][10];
 int memcp_op_sub_src_e[10][10];
 int memcp_op_sub_dst_s[10][10];
 int memcp_op_sub_dst_e[10][10];
 int memcp_op_sub_len[10][10];
unsigned char memcp_op_sub_type[10][10];

void HAL_SPI_RxHalfCpltCallback(SPI_HandleTypeDef * hspi){
  
   if (WR_REQ_PHASE==1){
    computeCircularAddr(0);
    memcp_half[memcp_indx]=0;
    rx_start_indx=(DMABlockSize/2)-1;
    memcp_indx++;
  } 
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef * hspi)
{
  if (WR_REQ_PHASE==1){
   
    computeCircularAddr(1);
    memcp_half[memcp_indx]=1;
    rx_start_indx=0;
    memcp_indx++;
  }  
}


/**
 * 
 * 
 * 
 */

void computeCircularAddr(int half){

  int offset=(DMABlockSize/2)*half;
  volatile int psrc_s=0;
  volatile int psrc_e=0;
  volatile int pdst_s=0;
  volatile int pdst_e=0;
  volatile int plen=0;
  
// WARNING write process IS FASTER than read, the tx_rx_gap will increase during the process,
// the best way is to keep track of the dst_s index = dst_s+plen after the first iteration

  if (memcp_indx==0 && rx_start_indx>=(DMABlockSize/2)*half  && rx_start_indx<(DMABlockSize/2)*(half+1)){
        psrc_s=rx_start_indx;
        psrc_e=(DMABlockSize/2-1)+offset;

        plen=psrc_e-psrc_s;
        
        if( (psrc_s+tx_rx_indx_gap)>0 ){
          pdst_s=(psrc_s+tx_rx_indx_gap)%DMABlockSize;
        }else{
          pdst_s=DMABlockSize+tx_rx_indx_gap;
        }

        pdst_e=(pdst_s+plen)%DMABlockSize;
        memcpy(dump_rx_buffer,DMA_BIT_RX_BUFFER+psrc_s,plen);
        processCircularBufferCopy(psrc_s,psrc_e,pdst_s,pdst_e,DMABlockSize,plen);
        total_byte_written+=plen;
        dst_s_index=(pdst_s+plen)%DMABlockSize;
    
    }else if(memcp_indx!=0){
      // copy the full first half
      psrc_s=offset;
      psrc_e=(DMABlockSize/2-1)+offset;

      /*if( (psrc_s+tx_rx_indx_gap)>0){
        pdst_s=(psrc_s+tx_rx_indx_gap)%DMABlockSize;
      }else{
        pdst_s=DMABlockSize+tx_rx_indx_gap; 
      }*/
      pdst_s=dst_s_index;
      plen=DMABlockSize/2;
      pdst_e=(pdst_s+plen-1)%DMABlockSize;
    
      processCircularBufferCopy(psrc_s,psrc_e,pdst_s,pdst_e,DMABlockSize,plen);
      total_byte_written+=plen;
      dst_s_index=(pdst_s+plen)%DMABlockSize;
    }

memcp_op_src_s[memcp_indx]=psrc_s;
memcp_op_src_e[memcp_indx]=psrc_e;
memcp_op_dst_s[memcp_indx]=pdst_s;
memcp_op_dst_e[memcp_indx]=pdst_e;
memcp_op_len[memcp_indx]=plen;
memcp_half[memcp_indx]=half;

}




void processCircularBufferCopy(unsigned int src_s,unsigned int src_e,unsigned int dst_s,unsigned int dst_e,unsigned int blocksize,unsigned int copylen){

int i=0;

/*
unsigned int memcp_op_sub_src_s[10][10];
unsigned int memcp_op_sub_src_e[10][10];
unsigned int memcp_op_sub_dst_s[10][10];
unsigned int memcp_op_sub_dst_e[10][10];
unsigned int memcp_op_sub_len[10];
memcp_op_sub_type[10][10]
*/
  if (src_e>src_s){
    if (dst_e>dst_s){
      //A1
      
      int src_len=src_e-src_s;
      memcpy(DMA_BIT_TX_BUFFER+dst_s,DMA_BIT_RX_BUFFER+src_s,src_len);

      memcp_op_sub_type[memcp_indx][i]='A';
      memcp_op_sub_src_s[memcp_indx][i]=src_s;
      memcp_op_sub_src_e[memcp_indx][i]=src_e;
      memcp_op_sub_dst_s[memcp_indx][i]=dst_s;
      memcp_op_sub_dst_e[memcp_indx][i]=dst_e;
      memcp_op_sub_len[memcp_indx][i]=src_len;
      memcp_sub_indx[memcp_indx]=1;
      
    }else{
      //A2 //B
      int dst_ep_len=DMABlockSize-dst_s;
      
      memcpy(DMA_BIT_TX_BUFFER+dst_s,DMA_BIT_RX_BUFFER+src_s,dst_ep_len);

      memcp_op_sub_type[memcp_indx][i]='B';
      memcp_op_sub_src_s[memcp_indx][i]=src_s;
      memcp_op_sub_src_e[memcp_indx][i]=src_s+dst_ep_len;
      memcp_op_sub_dst_s[memcp_indx][i]=dst_s;
      memcp_op_sub_dst_e[memcp_indx][i]=dst_s+dst_ep_len;
      memcp_op_sub_len[memcp_indx][i]=dst_ep_len;
      i++;
      
      memcpy(DMA_BIT_TX_BUFFER,DMA_BIT_RX_BUFFER+src_s+dst_ep_len,dst_e);
      
      memcp_op_sub_type[memcp_indx][i]='B';
      memcp_op_sub_src_s[memcp_indx][i]=src_s+dst_ep_len;
      memcp_op_sub_src_e[memcp_indx][i]=src_e;
      memcp_op_sub_dst_s[memcp_indx][i]=0;
      memcp_op_sub_dst_e[memcp_indx][i]=dst_e;
      memcp_op_sub_len[memcp_indx][i]=dst_e;
      
      i++;
      memcp_sub_indx[memcp_indx]=i;
    }
  }
  else {
        
    if (dst_e>dst_s){
      //B1 // C
      int src_ep_len=DMABlockSize-src_s;
     
      memcpy(DMA_BIT_TX_BUFFER+dst_s,DMA_BIT_RX_BUFFER+src_s,src_ep_len);

      memcp_op_sub_type[memcp_indx][i]='C';
      memcp_op_sub_src_s[memcp_indx][i]=src_s;
      memcp_op_sub_src_e[memcp_indx][i]=src_s+src_ep_len;
      memcp_op_sub_dst_s[memcp_indx][i]=dst_s;
      memcp_op_sub_dst_e[memcp_indx][i]=dst_s+src_ep_len;
      memcp_op_sub_len[memcp_indx][i]=src_ep_len;
      i++;
     
      memcpy(DMA_BIT_TX_BUFFER+dst_s+src_ep_len,DMA_BIT_RX_BUFFER,src_e);

      memcp_op_sub_type[memcp_indx][i]='C';
      memcp_op_sub_src_s[memcp_indx][i]=0;
      memcp_op_sub_src_e[memcp_indx][i]=src_e;
      memcp_op_sub_dst_s[memcp_indx][i]=dst_s+src_ep_len;
      memcp_op_sub_dst_e[memcp_indx][i]=dst_s+src_ep_len+src_e;
      memcp_op_sub_len[memcp_indx][i]=src_e;
      i++;
      memcp_sub_indx[memcp_indx]=i;

    }else{
      //B2_1 // D
      int src_ep_len=blocksize-src_s;
      int dst_ep_len=blocksize-dst_s;
          
      if (dst_ep_len>src_ep_len){
             
        memcpy(DMA_BIT_TX_BUFFER+dst_s,DMA_BIT_RX_BUFFER+src_s,src_ep_len);

        memcp_op_sub_type[memcp_indx][i]='D';
        memcp_op_sub_src_s[memcp_indx][i]=src_s;
        memcp_op_sub_src_e[memcp_indx][i]=src_s+src_ep_len;
        memcp_op_sub_dst_s[memcp_indx][i]=dst_s;
        memcp_op_sub_dst_e[memcp_indx][i]=dst_s+src_ep_len;
        memcp_op_sub_len[memcp_indx][i]=src_ep_len;
        i++;

             
        int dst_offset=dst_s+src_ep_len;
        memcpy(DMA_BIT_TX_BUFFER+dst_offset,DMA_BIT_RX_BUFFER,blocksize-dst_offset);

        memcp_op_sub_type[memcp_indx][i]='D';
        memcp_op_sub_src_s[memcp_indx][i]=0;
        memcp_op_sub_src_e[memcp_indx][i]=blocksize-dst_offset;
        memcp_op_sub_dst_s[memcp_indx][i]=dst_offset;
        memcp_op_sub_dst_e[memcp_indx][i]=dst_offset+blocksize-dst_offset;
        memcp_op_sub_len[memcp_indx][i]=src_ep_len;
        i++;
             
        int src_offset=DMABlockSize-dst_offset;
        memcpy(DMA_BIT_TX_BUFFER,DMA_BIT_RX_BUFFER+src_offset,src_ep_len);

        memcp_op_sub_type[memcp_indx][i]='D';
        memcp_op_sub_src_s[memcp_indx][i]=src_offset;
        memcp_op_sub_src_e[memcp_indx][i]=src_offset+src_ep_len;
        memcp_op_sub_dst_s[memcp_indx][i]=0;
        memcp_op_sub_dst_e[memcp_indx][i]=0+src_ep_len;
        memcp_op_sub_len[memcp_indx][i]=src_ep_len;
        i++;

        memcp_sub_indx[memcp_indx]=i;

      }else{
        //B2_2 //E
        
        memcpy(DMA_BIT_TX_BUFFER+dst_s,DMA_BIT_RX_BUFFER+src_s,dst_ep_len);

        memcp_op_sub_type[memcp_indx][i]='E';
        memcp_op_sub_src_s[memcp_indx][i]=src_s;
        memcp_op_sub_src_e[memcp_indx][i]=src_s+dst_ep_len;
        memcp_op_sub_dst_s[memcp_indx][i]=dst_s;
        memcp_op_sub_dst_e[memcp_indx][i]=dst_s+src_ep_len;
        memcp_op_sub_len[memcp_indx][i]=dst_ep_len;
        i++;
            
        int src_offset=src_s+src_ep_len;
        memcpy(DMA_BIT_TX_BUFFER,DMA_BIT_RX_BUFFER+src_offset,blocksize-src_offset);

        memcp_op_sub_type[memcp_indx][i]='E';
        memcp_op_sub_src_s[memcp_indx][i]=src_offset;
        memcp_op_sub_src_e[memcp_indx][i]=src_offset+blocksize-src_offset;
        memcp_op_sub_dst_s[memcp_indx][i]=0;
        memcp_op_sub_dst_e[memcp_indx][i]=blocksize-src_offset;
        memcp_op_sub_len[memcp_indx][i]=blocksize-src_offset;
        i++;
            
        int dst_offset=blocksize-src_offset;
        memcpy(DMA_BIT_TX_BUFFER+dst_offset,DMA_BIT_RX_BUFFER,src_e);

        memcp_op_sub_type[memcp_indx][i]='E';
        memcp_op_sub_src_s[memcp_indx][i]=0;
        memcp_op_sub_src_e[memcp_indx][i]=src_e;
        memcp_op_sub_dst_s[memcp_indx][i]=dst_offset;
        memcp_op_sub_dst_e[memcp_indx][i]=dst_offset+src_e;
        memcp_op_sub_len[memcp_indx][i]=src_e;
        i++;

        memcp_sub_indx[memcp_indx]=i;

      }
    }
  }
}

/**
  * @brief sort a new chainedlist of item alphabetically
  * @param list to be sorted
  * @retval the new list
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){

  UNUSED(htim);
  if (htim!=&htim2)
    return;

	if(HAL_GPIO_ReadPin(GPIOC, BTN_UP_Pin) == GPIO_PIN_RESET &&
    HAL_GPIO_ReadPin(GPIOC, BTN_DOWN_Pin) == GPIO_PIN_RESET &&
    HAL_GPIO_ReadPin(GPIOC, BTN_ENTR_Pin) == GPIO_PIN_RESET &&
    HAL_GPIO_ReadPin(GPIOB, BTN_RET_Pin) == GPIO_PIN_RESET 
  ){

		buttonDebounceState = true;
		HAL_TIM_Base_Stop_IT(&htim2);
	}
}

/**
  * @brief sort a new chainedlist of item alphabetically
  * @param list_t * plst
  * @retval None
  */
list_t * sortLinkedList(list_t * plst){

  list_t *  sorteddirChainedList = list_new();
  list_node_t *pItem;
  list_node_t *cItem;
  int z=0;
  int i=0;
  do{
    pItem=list_at(plst,0);
    for (i=0;i<plst->len;i++){
      cItem=list_at(plst,i);
      z=strcmp(pItem->val,cItem->val);
      if (z>0){
        pItem=cItem;
        i=0;
      }
    }
    
    list_rpush(sorteddirChainedList, list_node_new(pItem->val));
    list_remove(plst,pItem);
  }while (plst->len>0);
  list_destroy(plst);
  return sorteddirChainedList; 
}

/**
  * @brief Build & sort a new chainedlist of file/dir item based on the current path
  * @param path
  * @retval RET_OK/RET_ERR
  */
enum STATUS walkDir(char * path){
  DIR dir;
  FRESULT     fres;  
  
  fres = f_opendir(&dir, path);
 
  printf("Directory Listing:%s\n",path);
  if (fres != FR_OK){
    printf("Error f_opendir:%d\n", fres);
    return RET_ERR;
  }
    
  char * fileName;
  int len;
  lastlistPos=0;
  dirChainedList=list_new();

  if (fres == FR_OK){
      if (strcmp(path,"") && strcmp(path,"/")){
        fileName=malloc(128*sizeof(char));
        sprintf(fileName,"D|..");
        list_rpush(dirChainedList, list_node_new(fileName));
        lastlistPos++;
      }
      
      while(1){
        FILINFO fno;

        fres = f_readdir(&dir, &fno);
 
        if (fres != FR_OK)
          printf("Error f_readdir:%d\n", fres);
 
        if ((fres != FR_OK) || (fno.fname[0] == 0))
          break;
                                                                          // 256+2
        len=(int)strlen(fno.fname);                                       // Warning strlen
        
        if (((fno.fattrib & AM_DIR) && 
            !(fno.fattrib & AM_HID) && len>2 && fno.fname[0]!='.' ) ||     // Listing Directories & File with NIC extension
            (len>5 &&
            (!memcmp(fno.fname+(len-4),"\x2E\x4E\x49\x43",4)  ||           // .NIC
             !memcmp(fno.fname+(len-4),"\x2E\x6E\x69\x63",4)  ||           // .nic
             !memcmp(fno.fname+(len-4),"\x2E\x57\x4F\x5A",4)  ||           // .WOZ
             !memcmp(fno.fname+(len-4),"\x2E\x77\x6F\x7A",4)) &&           // .woz
             !(fno.fattrib & AM_SYS) &&                                    // Not System file
             !(fno.fattrib & AM_HID)                                       // Not Hidden file
             )
             
             ){
              
          fileName=malloc(64*sizeof(char));
          if (fno.fattrib & AM_DIR){
            fileName[0]='D';
            fileName[1]='|';
            strcpy(fileName+2,fno.fname);
          }else{
            fileName[0]='F';
            fileName[1]='|';
            memcpy(fileName+2,fno.fname,len);
            fileName[len+2]=0x0;
          }
            list_rpush(dirChainedList, list_node_new(fileName));
            lastlistPos++;
          }
       
        printf( "%c%c%c%c %10d %s/%s\n",
          ((fno.fattrib & AM_DIR) ? 'D' : '-'),
          ((fno.fattrib & AM_RDO) ? 'R' : '-'),
          ((fno.fattrib & AM_SYS) ? 'S' : '-'),
          ((fno.fattrib & AM_HID) ? 'H' : '-'),
          (int)fno.fsize, path, fno.fname);
 
      }
    }
  
  dirChainedList=sortLinkedList(dirChainedList);
  f_closedir(&dir);
  return RET_OK;
}

/**
  * @brief  Check if DiskII is Enable / Disable => Device Select is active low
  * @param None
  * @retval None
  */

char processDeviceEnableInterrupt(uint16_t GPIO_Pin){
  // The DEVICE_ENABLE signal from the Disk controller is activeLow
  // This signal is inverted to get active High signal trought LS14 U9E (P_11_I/P_10_O)
  // flgDeviceEnable=1 when Drive is activate 
  
  flgDeviceEnable=HAL_GPIO_ReadPin(DEVICE_ENABLE_GPIO_Port,GPIO_Pin);
  if (flgDeviceEnable==1){
    printf("flgDeviceEnable==1\n");
   // HAL_SPI_Transmit_DMA(&hspi1,DMA_BIT_TX_BUFFER,DMABlockSize);
   // HAL_SPI_Receive_DMA(&hspi3,DMA_BIT_RX_BUFFER,DMABlockSize);

  }else{
    printf("flgDeviceEnable==0\n");
    // HAL_SPI_DMAStop(&hspi1);
    //HAL_SPI_DMAStop(&hspi3);
  }
  return flgDeviceEnable;
}

/**
  * @brief  Trigger by External GPIO Interrupt 
  *         pointer to function according the page are linked to relevant function;   
  * @param GPIO_Pin
  * @retval None
  */
void processBtnInterrupt(uint16_t GPIO_Pin){     

  switch (GPIO_Pin){
    case BTN_UP_Pin:
      //ptrbtnDown(NULL);
      processBtnRet();
      printf("BTN UP\n"); 
      break;
    case BTN_DOWN_Pin:
      ptrbtnUp(NULL);
      printf("BTN DOWN\n");
      break;
    case BTN_ENTR_Pin:
      ptrbtnEntr(NULL);
      printf("BTN ENT\n");
      break;
    case BTN_RET_Pin:

      ptrbtnRet(NULL);
      printf("BTN RET\n");
      break;
    default:
      break;
  }       
}

/**
  * @brief  processPrevFSItem(), processNextFSItem(), processSelectFSItem()
  *         are functions linked to button DOWN/UP/ENTER to manage fileSystem information displayed,
  *         and trigger the action   
  * @param  Node
  * @retval None
  */

int currentClistPos;
int nextClistPos;

void processPrevFSItem(){
    if (currentClistPos==0){
      nextClistPos=lastlistPos-1;
    }else{
      nextClistPos=currentClistPos-1;
    }
    updateFSDisplay(0);
}

void processNextFSItem(){ 
    
    if (currentClistPos<(lastlistPos-1)){
      nextClistPos=currentClistPos+1;
    }else{
      nextClistPos=0;
    }

    updateFSDisplay(0);
}

void processSelectFSItem(){

  if (nextAction==FSDISP)
    return;

  list_node_t *pItem=NULL;
  
  pItem=list_at(dirChainedList, currentClistPos);
  sprintf(selItem,"%s",(char*)pItem->val);
  
  int len=strlen(currentFullPath);
  if (selItem[2]=='.' && selItem[3]=='.'){                // selectedItem is [UpDir];
    for (int i=len-1;i!=-1;i--){
      if (currentFullPath[i]=='/'){
        currentFullPath[i]=0x0;
        nextAction=FSDISP;
        break;
      }
      if (i==0)
        currentFullPath[0]=0x0;
    }
  }else if (selItem[0]=='D' && selItem[1]=='|'){        // selectedItem is a directory
    sprintf(currentFullPath+len,"/%s",selItem+2);
    nextAction=FSDISP;
  }else{
    swithPage(MOUNT,selItem);
  }
  printf("result |%s|\n",currentFullPath);
  
}

int toggle=1;
void processToogleOption(){
  
  if (toggle==1){
    toggle=0;
    toggleMountOption(0);
  }else{
    toggle=1;
    toggleMountOption(1);
  }
}

void processMountOption(){
  if (toggle==0){
    swithPage(FS,NULL);
    toggle=1;                               // rearm toggle switch
  }else{
    
    nextAction=IMG_MOUNT;                       // Mounting can not be done via Interrupt, must be done via the main thread
    swithPage(FS,NULL);
  }
}

void nothing(){
  __NOP();
}

void processBtnRet(){
  //printf("debugging write Buffer\n");
  sprintf(selItem,"F|FT.woz");
  nextAction=IMG_MOUNT; 
  return;

  if (currentPage==MOUNT){
    swithPage(FS,NULL);
  }else if (currentPage==FS){

  }
}

enum STATUS swithPage(enum page newPage,void * arg){

// Manage with page to display and the attach function to button Interrupt  
  
  switch(newPage){
    case FS:
      initFSScreen(currentFullPath);
      updateFSDisplay(1);
      ptrbtnUp=processNextFSItem;
      ptrbtnDown=processPrevFSItem;
      ptrbtnEntr=processSelectFSItem;
      //ptrbtnRet=nothing;
      ptrbtnRet=processBtnRet;
      currentPage=FS;
      break;
    case MENU:
      break;
    case IMAGE:
       initIMAGEScreen(selItem+2,0);
       ptrbtnUp=nothing;
       ptrbtnDown=nothing;
       ptrbtnRet=nothing;
       ptrbtnRet=processBtnRet;
       currentPage=IMAGE;
      break;
    case MOUNT:
      mountImageScreen((char*)arg+2);
      ptrbtnEntr=processMountOption;
      ptrbtnUp=processToogleOption;
      ptrbtnDown=processToogleOption;
      ptrbtnRet=processBtnRet;
      currentPage=MOUNT;
      break;
    default:
      return RET_ERR;
      break;
  }
  return RET_OK;
}


 // ONLY FOR DEBUGGING
/*
int k1=0;
unsigned char dbgStp[1024];
unsigned char dbgNewPosition[1024];
unsigned char dbgLastPosition[1024];
unsigned char dbgPhtrk[1024];
unsigned char dbgTrk[1024];
int dbg_move[1024];
*/

// Magnet States --> Stepper Motor Position
//
//                N
//               0001
//        NW      |      NE
//       1001     |     0011
//                |
// W 1000 ------- o ------- 0010 E
//                |
//       1100     |     0110
//        SW      |      SE
//               0100
//                S

const int magnet2Position[16] = {
//   0000 0001 0010 0011 0100 0101 0110 0111 1000 1001 1010 1011 1100 1101 1110 1111
       -1,   0,   2,   1,   4,  -1,   3,  -1,   6,   7,  -1,  -1,   5,  -1,  -1,  -1
};

const int position2Direction[8][8] = {               // position2Direction[X][Y] :X ROW Y: COLUMN 
//     N  NE   E  SE   S  SW   W  NW
//     0   1   2   3   4   5   6   7
    {  0,  1,  2,  3,  0, -3, -2, -1 }, // 0 N
    { -1,  0,  1,  2,  3,  0, -3, -2 }, // 1 NE
    { -2, -1,  0,  1,  2,  3,  0, -3 }, // 2 E
    { -3, -2, -1,  0,  1,  2,  3,  0 }, // 3 SE
    {  0, -3, -2, -1,  0,  1,  2,  3 }, // 4 S
    {  3,  0, -3, -2, -1,  0,  1,  2 }, // 5 SW
    {  2,  3,  0, -3, -2, -1,  0,  1 }, // 6 W
    {  1,  2,  3,  0, -3, -2, -1,  0 }, // 7 NW
};

void processDiskHeadMoveInterrupt(uint16_t GPIO_Pin){

  if(flgDeviceEnable==0)
    return;
  //printf("E0\n");
  //fflush(stdout);
  
  unsigned char stp=(GPIOA->IDR&0b0000000000001111);

  int newPosition=magnet2Position[stp];

  if (newPosition>=0){
    int lastPosition=ph_track&7;
    int move=position2Direction[lastPosition][newPosition];
    
    ph_track+= move;
  
    if (ph_track<0)
      ph_track=0;

    if (ph_track>160)                                                                 // <!> to be changed according to Woz info
      ph_track=160;                                             
                                                      
    intTrk=getTrackFromPh(ph_track);
  
    // Only for debugging to be removed
   /* if (k1<1024){
      dbg_move[k1]=move;
      dbgPhtrk[k1]=ph_track;
      dbgStp[k1]=stp;
      dbgLastPosition[k1]=lastPosition;
      dbgNewPosition[k1]=newPosition;
    }

    k1++;
    //
    */
  }
  //printf("E1\n");
  //fflush(stdout);
}

enum STATUS mountImagefile(char * filename){
  int l=0;
  
  flgImageMounted=0;
  if (filename==NULL)
    return RET_ERR;

  FRESULT fr;
  FILINFO fno;
  
  printf("Mounting image %s\n",filename);
  
  fr = f_stat(filename, &fno);
  switch (fr) {
    case FR_OK:
        printf("Size: %lu\n", fno.fsize);
        printf("Timestamp: %u-%02u-%02u, %02u:%02u\n",
               (fno.fdate >> 9) + 1980, fno.fdate >> 5 & 15, fno.fdate & 31,
               fno.ftime >> 11, fno.ftime >> 5 & 63);
        printf("Attributes: %c%c%c%c%c\n",
               (fno.fattrib & AM_DIR) ? 'D' : '-',
               (fno.fattrib & AM_RDO) ? 'R' : '-',
               (fno.fattrib & AM_HID) ? 'H' : '-',
               (fno.fattrib & AM_SYS) ? 'S' : '-',
               (fno.fattrib & AM_ARC) ? 'A' : '-');
        break;
    case FR_NO_FILE:
    case FR_NO_PATH:
        printf("\"%s\" does not exist.\n", filename);
        return RET_ERR;
        break;
    default:
        printf("An error occured. (%d)\n", fr);
        return RET_ERR;
  }

  l=strlen(filename);
  if (l>4 && 
      (!memcmp(filename+(l-4),"\x2E\x4E\x49\x43",4)  ||           // .NIC
       !memcmp(filename+(l-4),"\x2E\x6E\x69\x63",4))){            // .nic
     //DMABlockSize=16*416;
     if (mountNicFile(filename)!=RET_OK)
        return RET_ERR;
    
     getSDAddr=getSDAddrNic;
     getTrackBitStream=getNicTrackBitStream;
     getTrackFromPh=getNicTrackFromPh;
  }else if (l>4 && 
      (!memcmp(filename+(l-4),"\x2E\x57\x4F\x5A",4)  ||           // .WOZ
       !memcmp(filename+(l-4),"\x2E\x77\x6F\x7A",4))) {           // .woz
    //DMABlockSize=13*512;
    if (mountWozFile(filename)!=RET_OK)
      return RET_ERR;

    getSDAddr=getSDAddrWoz;
    getTrackBitStream=getWozTrackBitStream;
    setTrackBitStream=setWozTrackBitStream;
    getTrackFromPh=getWozTrackFromPh;
    
  }else{
    return RET_ERR;
  }

  printf("Mount image:OK\n");
  flgImageMounted=1;
  return RET_OK;
}

enum STATUS initeDMABuffering(){
  
  if (flgImageMounted!=1){
    return RET_ERR;
  }
  HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);   
  HAL_SPI_DMAStop(&hspi1); 
  flgBeaming=0;
  memset(DMA_BIT_TX_BUFFER,0,sizeof(char)*DMABlockSize);
  memset(DMA_BIT_RX_BUFFER,0,sizeof(char)*DMABlockSize);
  memset(read_track_data_bloc,0,sizeof(char)*3*RawSDTrackSize);

  for (int i=0;i<3;i++){
    woz_sel_trk[i]=-1;
  }
  
  woz_block_sel_012=0;
  getTrackBitStream(0,read_track_data_bloc);
  woz_sel_trk[0]=0; 
      
  getTrackBitStream(1,read_track_data_bloc+RawSDTrackSize);
  woz_sel_trk[1]=1;

  getTrackBitStream(2,read_track_data_bloc+2*RawSDTrackSize);
  woz_sel_trk[2]=2; 
 
  printf("start PrevTrk=%d; intTrk=%d\n",prevTrk,intTrk);

  HAL_GPIO_WritePin(WR_PROTECT_GPIO_Port,WR_PROTECT_Pin,GPIO_PIN_RESET);      // WRITE_PROTECT is disable


  TIM1->CCR1 = 64;                                                                  // Set the Duty Cycle 
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);                                // Start the PWN to for the Logic Analyzer to decode SPI with Pulse 250 Khz
  
  for (int i=0;i<45;i++)                                                            // Used for Logic Analyzer and sync between SPI and Timer1 PWM
    __NOP();                                                                        // Macro to NOP assembly code

  memcpy(DMA_BIT_TX_BUFFER,read_track_data_bloc,DMABlockSize); 
  HAL_SPI_Transmit_DMA(&hspi1,DMA_BIT_TX_BUFFER,DMABlockSize);
  HAL_SPI_Receive_DMA(&hspi3,DMA_BIT_RX_BUFFER,DMABlockSize);

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1); 
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);   
  
  flgBeaming=1;
  return RET_OK; 
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  FATFS fs;
  FRESULT fres;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI2_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_FATFS_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_SPI3_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */

 
  uint8_t Test[] = "\n\n************** BOOTING ****************\r\n";                        // Data to send

  HAL_UART_Transmit(&huart1,Test,sizeof(Test),10);             // Sending in normal mode
  
  //HAL_Delay(1000);
 
  srand( time( NULL ) );
  printf("this is the sound of sea\n");
  printf("\n\n\n");
  EnableTiming();                                                                   // Enable WatchDog to get precise CPU Cycle counting
  initScreen();                                                                     // I2C Screen init

  dirChainedList = list_new();                                                      // ChainedList to manage File list in current path
  currentClistPos=0;                                                                // Current index in the chained List
  lastlistPos=0;                                                                    // Last index in the chained list

  int trk=0;
  prevTrk=35; 
                                                                     
  unsigned long t1,t2,diff1,diff2;
  currentFullPath[0]=0x0;                                                            // Root is ""
  fres = f_mount(&fs, "", 1);                                       
  if (fres == FR_OK) {
    walkDir(currentFullPath);
  }else{
    printf("Error mounting sdcard %d\n",fres);
  }

  csize=fs.csize;
  database=fs.database;
  
  printf("Loading config file");
  //loadConfigFile();
  
  //char *imgFile=(char*)getConfigParamStr(configParams,"lastImageFile");
  //if (imgFile!=NULL){
  //  mountImagefile(imgFile);
  //  initeDMABuffering();
  //  swithPage(IMAGE,NULL);
  //}else{
  //if (mountImagefile("FT.woz")!=RET_OK){
    
    
    if (mountImagefile("/Blank.woz")!=RET_OK){
    
    //if (mountImagefile("Locksmithcrk.nic")!=RET_OK){
      printf("Mount Image Error\n");
    }
  
    if (flgImageMounted==1){
      initeDMABuffering();
      processDeviceEnableInterrupt(DEVICE_ENABLE_Pin);
    }
    
    swithPage(FS,NULL);
  //}

  printf("Inside the twilight\n");
  fflush(stdout);
  //dumpBuf(DMA_BIT_TX_BUFFER,1,512);
 
    
  // Todo :
  // Add DeviceEnable to Interrupt
  // Init buffer on mount image change
  // Config based last image mount
  
  unsigned int f=0,fp1=0,fm1=0;
  unsigned long cAlive=0;


  memset(dump_rx_buffer,0,DMABlockSize);
  //int kk=0;
  while (1){
      
    /*
    if ((k1+1)%128==0 && k1<1024){
      for (int i=0;i<128;i++){
        char * cstp=byte_to_binary(dbgStp[i]);
        printf("%04d stp:%02d=>%s newPosition:%d, lastPosition:%d, ph_track:%03d trk:%02d move:%d \n",i,dbgStp[i],cstp,dbgNewPosition[i],dbgLastPosition[i], dbgPhtrk[i],dbgTrk[i],dbg_move[i]);
        free(cstp);
      }
    }
    */
  // continue;
    
    /*if (kk==99){
      kk=0;
      printf("Looping enable=%d prevTrk:%d!=intTrk:%d flg:%d\n",isDiskIIDisable(),prevTrk,intTrk,fldImageMounted);
    }
    kk++;
   */
    /*if (isDiskIIDisable()==1){
      printf("here\n");
    }*/
    if (flgDeviceEnable==1 && prevTrk!=intTrk && flgImageMounted==1){  
      trk=intTrk;                                                                    // Track has changed, but avoid new change during the process
      f=0;                                                                           // flag for main track data found withing the adjacent track
      
      //DWT->CYCCNT = 0;                                                               // Reset cpu cycle counter
      //t1 = DWT->CYCCNT;
      //printf("A0A\n");
      HAL_SPI_DMAPause(&hspi1);                                                      // Pause the current DMA
      //printf("A0B\n");
      if (trk==255){    
        for (int i=0;i<DMABlockSize;i++){
          DMA_BIT_TX_BUFFER[i]=rand();
        }                                                                            // If Track is 255 then stuf it with random bit                                                                             
        
        HAL_SPI_DMAResume(&hspi1);
        flgwhiteNoise=1;                                                              // Enable repeat random bit in the buffer with DMA half/cmplt buffer interrupt
        t2 = DWT->CYCCNT;
        diff1 = t2 - t1;
        continue;
      }
      flgwhiteNoise=0;
      
      // FIRST MANAGE THE MAIN TRACK & RESTORE AS QUICKLY AS POSSIBLE THE DMA
      
      for (int i=0;i<3;i++){
        if (woz_sel_trk[i]==trk){
          f=1;
          woz_block_sel_012=i;
          break;
        }
      }

      if (f!=1){
        woz_block_sel_012=1;
        getTrackBitStream(trk,read_track_data_bloc+woz_block_sel_012*RawSDTrackSize);
        woz_sel_trk[1]=trk;
          
      }
      //printf("A1\n");

      memcpy(DMA_BIT_TX_BUFFER,read_track_data_bloc+woz_block_sel_012*RawSDTrackSize,DMABlockSize);      // copy the new track data to the DMA Buffer
      HAL_SPI_DMAResume(&hspi1); 
      //dumpBuf(DMA_BIT_TX_BUFFER,666,512);
      //t2 = DWT->CYCCNT;
      //diff1 = t2 - t1;

      //DWT->CYCCNT = 0;                                                                            // Reset cpu cycle counter
      //t1 = DWT->CYCCNT;

      // THEN MANAGE THE MAIN TRACK & RESTORE AS QUICKLY AS POSSIBLE THE DMA

      int selP1=(woz_block_sel_012+1)%3;                                                          // selector number for the track above within 0,1,2
      int selM1=(woz_block_sel_012-1)%3;                                                          // selector number for the track below within 0,1,2

      if (woz_sel_trk[selP1]==(trk+1)){
        fp1=1;
      }
      else if (trk!=35 && woz_sel_trk[selP1]!=(trk+1)){                                           // check if we need to load the track above                                                     
        getTrackBitStream(trk+1,read_track_data_bloc+selP1*RawSDTrackSize);     
        woz_sel_trk[selP1]=(trk+1);                                                               // change the trk in the selector
      }
     //printf("A2\n");
      if (woz_sel_trk[selM1]==(trk-1)){
        fm1=1;
      }
      else if (trk!=0 && woz_sel_trk[selM1]!=(trk-1)){                                            // check if we need to load the track above
        getTrackBitStream(trk-1,read_track_data_bloc+selM1*RawSDTrackSize);     
        woz_sel_trk[selM1]=(trk-1);                                                               // change the trk in the selector
      }
      //printf("A3\n");
      //t2 = DWT->CYCCNT;
      //diff2 = t2 - t1;
      //printf("ph:%02d newTrak:%02d, prevTrak:%02d, %02d-%02d-%02d %d %d-%d-%d d1:%ld d2:%ld\n",ph_track,trk,prevTrk,woz_sel_trk[selM1],woz_sel_trk[woz_block_sel_012],woz_sel_trk[selP1],woz_block_sel_012,fm1,f,fp1,diff1,diff2);
      printf("track change prevtrk:%d => trk:%d\n",prevTrk,trk);
      prevTrk=trk;

    }else if (flgImageMounted==0){
      //printf("Fuck\n");
      
    }

    else if (nextAction!=NONE){                                         // Several action can not be done on Interrupt
      
      switch(nextAction){
        
        case WRITE_TRK:
          long offset=3*512+intTrk*13*512;
          writeTrkFile("/Blank.woz",DMA_BIT_TX_BUFFER,offset);
          /*if(setTrackBitStream(intTrk,DMA_BIT_TX_BUFFER)!=0){
            printf("error writing the track\n");
          }else{
            printf("good write\n");
          }*/
          nextAction=NONE;
          //printf("write track to file");

          break;
        case DUMP_TX:
          char filename[32];
          sprintf(filename,"dump_tx_trk%d_p%d.bin",intTrk,p);

          dumpBufFile(filename,DMA_BIT_TX_BUFFER,DMABlockSize);

          sprintf(filename,"dump_rx_trk%d_p%d.bin",intTrk,p);
          dumpBufFile(filename,dump_rx_buffer,DMABlockSize);
          memset(DMA_BIT_RX_BUFFER,0,DMABlockSize);                      // WARNING TO BE TESTED
          memset(dump_rx_buffer,0,DMABlockSize);     
          nextAction=NONE;
          break;
        case FSDISP:
          list_destroy(dirChainedList);
          walkDir(currentFullPath);
          currentClistPos=0;
          initFSScreen("");
          updateFSDisplay(1); 
          nextAction=NONE;
          break;
        case IMG_MOUNT:
          int len=strlen(currentFullPath)+strlen(selItem)+1;
          char * tmp=(char *)malloc(len*sizeof(char));
          sprintf(tmp,"%s/%s",currentFullPath,selItem+2);

          mountImagefile(tmp);
          initeDMABuffering();
          setConfigParamStr(configParams,"lastImageFile",tmp);
          saveConfigFile();

          free(tmp);
          swithPage(IMAGE,NULL);
          nextAction=NONE;
          break;
        default:
          break;
      }
    }else{
      cAlive++;
      if (cAlive==50000000){
        printf(".\n");
        //printf("still alive diskEnable=%d,prevTrk:%d!=%d ImgMounted:%d\n",flgDeviceEnable,prevTrk,intTrk,flgImageMounted);
        cAlive=0;
      }
    }
  }
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 128;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_SLAVE;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  /* SPI2 parameter configuration*/
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_HARD_OUTPUT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief SPI3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI3_Init(void)
{

  /* USER CODE BEGIN SPI3_Init 0 */

  /* USER CODE END SPI3_Init 0 */

  /* USER CODE BEGIN SPI3_Init 1 */

  /* USER CODE END SPI3_Init 1 */
  /* SPI3 parameter configuration*/
  hspi3.Instance = SPI3;
  hspi3.Init.Mode = SPI_MODE_SLAVE;
  hspi3.Init.Direction = SPI_DIRECTION_2LINES_RXONLY;
  hspi3.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi3.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi3.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi3.Init.NSS = SPI_NSS_SOFT;
  hspi3.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi3.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi3.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi3.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI3_Init 2 */

  /* USER CODE END SPI3_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 255;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 128;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 32000;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 100;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_OC_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_TIMING;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_OC_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 255;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 128;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 3;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 2;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA2_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(WR_PROTECT_GPIO_Port, WR_PROTECT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : BTN_ENTR_Pin BTN_UP_Pin BTN_DOWN_Pin */
  GPIO_InitStruct.Pin = BTN_ENTR_Pin|BTN_UP_Pin|BTN_DOWN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : STEP0_Pin STEP1_Pin STEP2_Pin STEP3_Pin */
  GPIO_InitStruct.Pin = STEP0_Pin|STEP1_Pin|STEP2_Pin|STEP3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : DEVICE_ENABLE_Pin SD_EJECT_Pin */
  GPIO_InitStruct.Pin = DEVICE_ENABLE_Pin|SD_EJECT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : WR_PROTECT_Pin */
  GPIO_InitStruct.Pin = WR_PROTECT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(WR_PROTECT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_RET_Pin */
  GPIO_InitStruct.Pin = BTN_RET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(BTN_RET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : WR_REQ_Pin */
  GPIO_InitStruct.Pin = WR_REQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(WR_REQ_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief  Callback function for External interrupt
  * @param  GPIO_Pin
  * @retval None
  */

int keep_tx;
int keep_rx;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{

  //printf("startr here 0 %d\n",GPIO_Pin);
  if( GPIO_Pin == STEP0_Pin   ||               // Step 0 PB8
      GPIO_Pin == STEP1_Pin   ||               // Step 1 PB9
      GPIO_Pin == STEP2_Pin   ||               // Step 2 PB10
      GPIO_Pin == STEP3_Pin 
     ) {            // Step 3 PB11
    
    processDiskHeadMoveInterrupt(GPIO_Pin);
   
  }else if (GPIO_Pin==DEVICE_ENABLE_Pin){
    processDeviceEnableInterrupt(DEVICE_ENABLE_Pin);
  }else if ((GPIO_Pin == BTN_RET_Pin  ||      // BTN_RETURN
            GPIO_Pin == BTN_ENTR_Pin  ||      // BTN_ENTER
            GPIO_Pin == BTN_UP_Pin    ||      // BTN_UP
            GPIO_Pin == BTN_DOWN_Pin          // BTN_DOWN
            )&& buttonDebounceState==true){
           
    buttonDebounceState=false;
    HAL_TIM_Base_Start_IT(&htim2);
    processBtnInterrupt(GPIO_Pin);

  } else if (GPIO_Pin == WR_REQ_Pin){
  
    if (WR_REQ_PHASE==0){                                                 // Instruction order are critical
      rx_start_indx= DMABlockSize - __HAL_DMA_GET_COUNTER(hspi3.hdmarx);
      tx_start_indx= DMABlockSize - __HAL_DMA_GET_COUNTER(hspi1.hdmatx);
      
      if (flgDeviceEnable==0)                       // No Write is Motor is not running
        return;

      WR_REQ_PHASE=1;
      memcp_indx=0;
      
      //__HAL_DMA_SET_COUNTER(hspi3.hdmarx, 0);
      //__HAL_DMA_SET_COUNTER(hspi1.hdmatx, 0);


      
      keep_tx=tx_start_indx;
      keep_rx=rx_start_indx;

      tx_rx_indx_gap=tx_start_indx-rx_start_indx;
      total_byte_written=0;

    }else{
      rx_end_indx =  DMABlockSize - __HAL_DMA_GET_COUNTER(hspi3.hdmarx);
      tx_end_indx =  DMABlockSize - __HAL_DMA_GET_COUNTER(hspi1.hdmatx);                                                                         // Should be on falling Edge
      
      WR_REQ_PHASE=0;

      volatile int fsrc_s=0;
      volatile int fsrc_e=0;
      volatile int fdst_s=0;
      volatile int fdst_e=0;
      volatile int flen=0;
      //memcpy(dump_rx_buffer,DMA_BIT_RX_BUFFER,DMABlockSize);
      
      fsrc_s=rx_start_indx;
      fsrc_e=rx_end_indx;

      /*
      if( (fsrc_s+tx_rx_indx_gap)>0){
        fdst_s=(fsrc_s+tx_rx_indx_gap)%DMABlockSize;
      }else{
        fdst_s=DMABlockSize+tx_rx_indx_gap; 
      }
      */
      fdst_s=dst_s_index;
      flen=fsrc_e-fsrc_s;
      fdst_e=(dst_s_index+flen)%DMABlockSize;
      

      processCircularBufferCopy(fsrc_s,fsrc_e,fdst_s,fdst_e,DMABlockSize,flen);
      total_byte_written+=flen;
      
      memcp_op_src_s[memcp_indx]=fsrc_s;
      memcp_op_src_e[memcp_indx]=fsrc_e;
      memcp_op_dst_s[memcp_indx]=fdst_s;
      memcp_op_dst_e[memcp_indx]=fdst_e;
      memcp_op_len[memcp_indx]=flen;
      memcp_half[memcp_indx]=2;
      
      memcp_indx++;
      
      //nextAction=DUMP_TX;
      nextAction=WRITE_TRK;
      p++;
      for (int i=0;i<memcp_indx;i++){
        printf("cpy i:%02d half:%d BlocSize:%d src(s:%04d,e:%04d) => dst(s:%04d,e:%04d) l:%04d  tx_start:0x%04x tx_stop:%04x tx_rx_gap:%03d total_byte:%04d\n",i,memcp_half[i],DMABlockSize, memcp_op_src_s[i],memcp_op_src_e[i],memcp_op_dst_s[i],memcp_op_dst_e[i], memcp_op_len[i],memcp_op_dst_s[i],memcp_op_dst_e[i], tx_rx_indx_gap,total_byte_written);
        for (int j=0;j<memcp_sub_indx[i];j++){
          printf("cpy j:%02d type:%c src(s:%04d,e:%04d) => dst(s:%04d, e:%04d) l:%04d \n",j,memcp_op_sub_type[i][j], memcp_op_sub_src_s[i][j],memcp_op_sub_src_e[i][j],memcp_op_sub_dst_s[i][j],memcp_op_sub_dst_e[i][j], memcp_op_sub_len[i][j]);
        }
        printf("\n");
      }
      printf("WR_REQ Ends ktx:%d 0x%04x krx:%d 0x%04x IntTRK:%d [RX S:%d, E:%d ] [TX S:%d, E:%d] \n",keep_tx,keep_tx,keep_rx,  keep_rx,intTrk,keep_rx,rx_end_indx,keep_tx,tx_end_indx);
    }

  }else {
      __NOP();
  }
}

/**
  * @brief  Retargets the C library printf function to the USART.
  * @param  None
  * @retval None
  */
PUTCHAR_PROTOTYPE
{
  /* Place your implementation of fputc here */
  /* e.g. write a character to the USART1 and Loop until the end of transmission */
  
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 0xFFFF);
  return ch;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
    printf("oups I'm fucked\n");
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
