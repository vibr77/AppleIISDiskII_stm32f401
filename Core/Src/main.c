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

Current status: NOT WORKING
+ woz file support in progress

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
#include "woz.h"
#include "configFile.h"
//#include "parson.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#undef TIM2_PERIOD
#define TIM2_PERIOD 72-1
#define CPU_FREQ    72
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;
DMA_HandleTypeDef hdma_spi1_tx;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;

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
/* USER CODE BEGIN PFP */
bool buttonDebounceState=true;

volatile int ph_track=0;                                    // SDISK Physical track 0 - 139
volatile int prevPh_track=0;
                                      
unsigned char prevTrk=0;                                    // prevTrk to keep track of the last head track
volatile unsigned char trkChangeFlg=0;                      // Head move & trk change according to TMAP Woz
unsigned char mountedImageFile=0;                           // Image file mount status flag

const unsigned int DMABlockSize=6656;//6592; // 6656                       // Size of the DMA Buffer => full track width with 13 block of 512
unsigned char DMA_BIT_BUFFER[8192];                         // DMA Buffer from the SPI
unsigned int woz_block_sel_012=0;                           // current index of the current track related to woz_track_data_bloc[3][6656];
int woz_sel_trk[3];                                         // keep track number in the selctor
unsigned int whiteNoise=0; 
                              
                                                            // trk 0-35
long database=0;                                            // start of the data segment in FAT
int csize=0;                                                // Cluster size

const unsigned int blockNumber=13;                           // Number of block over 13 data block
                                                             // Char Array containing 3 tracks of 13 blocks 512 Bytes each
unsigned int currentBlock=0;
unsigned int nextBlock=0; 

int (*getTrackBitStream)(int,unsigned char*);

long (*getSDAddr)(int ,int ,int , long);
int  (*getTrackFromPh)(int);

enum page currentPage=0;
enum action nextAction=NONE;
void (*ptrbtnUp)(void *);
void (*ptrbtnDown)(void *);
void (*ptrbtnEntr)(void *);
void (*ptrbtnRet)(void *);



extern unsigned int fatCluster[20];
extern __uint16_t BLK_startingBlocOffset[160];
extern __uint8_t TMAP[160];
extern woz_info_t wozFile;

extern JSON_Object *configParams;
char selItem[256];
char currentFullPath[1024];                                     // Path from root
                                                                // Path
int lastlistPos;
list_t * dirChainedList;
int updateFSFlag;

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

void cmd17GetDataBlock(long memoryAdr,unsigned char *buffer){
  
  int ret=0;
  if ((ret=getSDCMD(CMD17, memoryAdr) == 0))
    getSDDataBlock((BYTE *)buffer, 512); // SPI command to read a block
  return;
}

void cmd18GetDataBlocks(long memoryAdr,unsigned char * buffer,int count){
  
  int ret=0;
  if ((ret=getSDCMD(CMD18, memoryAdr) == 0)){
    do{
      if (!getSDDataBlock((BYTE *)buffer, 512)){
        printf("dirdel issue 2\n");
        break;
      }
        
        buffer += 512;
    } while (--count);
      /* STOP_TRANSMISSION */
      getSDCMD(CMD12, 0);
  }
  else{
    printf("dirdel issue 1\n");
  }
}

void cmd18GetDataBlocksBareMetal(long memoryAdr,unsigned char * buffer,int count){
  
  int ret=0;
  if ((ret=getSDCMD(CMD18, memoryAdr) == 0)){
    do{
      if (!getSDDataBlockBareMetal((BYTE *)buffer, 512)){
        printf("SDCard getSDDataBlockBareMetal Error\n");
        break;
      }
        
        buffer += 512;
    } while (--count);
      /* STOP_TRANSMISSION */
      getSDCMD(CMD12, 0);
  }
  else{
    printf("SDCard cmd18 Error\n");
  }
}

/*
 The next 2 functions are called during the DMA transfer from Memory to SPI1
 2 buffers are used to managed the timing of getting the data from the SDCard & populating the buffer
 At half of the transsfer, a new sector is requested via "prepareNewSector=1"
 At the end of populating the buffer, a switch is made to the other buffer
*/

void HAL_SPI_TxHalfCpltCallback(SPI_HandleTypeDef *hspi){
  
  if (whiteNoise==1){                                // Enable non repeatable white noise on the first half track
    for (int i=0;i<DMABlockSize/2;i++){
          DMA_BIT_BUFFER[i]=rand();
    }

  }
  return;
}

void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi){

  if (whiteNoise==1){                                // Enable non repeatable white noise on the second half track
    for (int i=DMABlockSize/2;i<DMABlockSize;i++){   
          DMA_BIT_BUFFER[i]=rand();
    }
  }
  return;
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
  * @retval None
  */
void walkDir(char * path){
  DIR dir;
  FRESULT     fres;  
  
  fres = f_opendir(&dir, path);
 
  printf("Directory Listing:%s\n",path);
  if (fres != FR_OK)
    printf("Error f_opendir:%d\n", fres);

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
}

/**
  * @brief  Check if DiskII is Enable / Disable => Device Select is active low
  * @param None
  * @retval None
  */
int isDiskIIDisable(){
  return HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_4);
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
      ptrbtnDown(NULL);
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

  if (updateFSFlag==1)
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
  if (currentPage==MOUNT){
    swithPage(FS,NULL);
  }else if (currentPage==FS){

  }
}

void swithPage(enum page newPage,void * arg){

// Manage with page to display and the attach function to button Interrupt  
  
  switch(newPage){
    case FS:
      initFSScreen(currentFullPath);
      updateFSDisplay(1);
      ptrbtnUp=processNextFSItem;
      ptrbtnDown=processPrevFSItem;
      ptrbtnEntr=processSelectFSItem;
      ptrbtnRet=nothing;
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
      break;


  }
}


/* // ONLY FOR DEBUGGING
int k1=0;
unsigned char dbg_stp[1024];
unsigned char dbg_newStp[1024];
unsigned char dbg_prevStp[1024];
unsigned char dbg_phtrk[1024];
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

void processDiskHeadMove(uint16_t GPIO_Pin){

  unsigned char stp=0;
  int  move=0;
  
  /*
  if (isDiskIIDisable())
    return;
  */

  stp=(GPIOB->IDR&0b0000000000001111);
  int newPosition=magnet2Position[stp];
  int lastPosition=ph_track&7;

  if (newPosition>=0){
    move=position2Direction[lastPosition][newPosition];
    ph_track+= move;
  }
  if (ph_track<0)
    ph_track=0;

  if (ph_track>139)                                                                 // <!> to be changed according to Woz info
    ph_track=139;                                             

                                                             
  int trk=getTrackFromPh(ph_track);
  //printf("trk:%02d ph_track:%03d\n",trk,ph_track);
  if(/*trk!=255 && */ trk!=prevTrk){
    trkChangeFlg=1;
    //printf("fuck %d\n",trk);
    //prevTrk=trk;
  }
  /*
  if (k1<1024){
    dbg_move[k1]=move;
    dbg_phtrk[k1]=ph_track;
    dbg_stp[k1]=stp;
    dbg_prevStp[k1]=lastPosition;
    dbg_newStp[k1]=newPosition;
  }

  k1++;
  */

}

long getSDAddrWoz(int trk,int block,int csize, long database){
  long rSector=-1;
  if (wozFile.version==2){
    int long_sector = BLK_startingBlocOffset[trk] + block;
    int long_cluster = long_sector >> 6;
    int ft = fatCluster[long_cluster];
    rSector=database+(ft-2)*csize+(long_sector & (csize-1));
  }else if (wozFile.version==1){
    int long_sector = 13*trk;                                // 13 block of 512 per track
    int long_cluster = long_sector >> 6;
    int ft = fatCluster[long_cluster];
    rSector=database+(ft-2)*csize+(long_sector & (csize-1));
  }
  
  return rSector;
}

long getSDAddrNic(int trk,int block,int csize, long database){
  int long_sector = trk*16;
  int long_cluster = long_sector >> 6;
  int ft = fatCluster[long_cluster];
  long rSector=database+(ft-2)*csize+(long_sector & (csize-1));
  return rSector;
}

int getNicTrackBitStream(int trk,unsigned  char* buffer){
  int addr=getSDAddrNic(trk,0,csize,database);
  if (addr==-1){
    printf("Error getting SDCard Address for nic\n");
    return -1;
  }
  
  unsigned char * tmp2=(unsigned char*)malloc(16*512*sizeof(char));
  if (tmp2==NULL){
    printf("Error memory alloaction getNicTrackBitStream: tmp2:8192 Bytes");
    return -1;
  }

  cmd18GetDataBlocksBareMetal(addr,tmp2,16);

  for (int i=0;i<16;i++){
    memcpy(buffer+i*412,tmp2+i*512,412);
  }
  free(tmp2);
  return 1;
}

int getWozTrackBitStream(int trk,unsigned char * buffer){
  int addr=getSDAddrWoz(trk,0,csize,database);
  
  if (addr==-1){
    printf("Error getting SDCard Address for woz\n");
    return -1;
  }
  
  if (wozFile.version==2){
    cmd18GetDataBlocksBareMetal(addr,buffer,blockNumber);
  }else if (wozFile.version==1){
    unsigned char * tmp2=(unsigned char*)malloc(14*512*sizeof(char));
    if (tmp2==NULL){
      printf("Error memory alloaction getNicTrackBitStream: tmp2:8192 Bytes");
      return -1;
    }

    cmd18GetDataBlocksBareMetal(addr,tmp2,blockNumber+1);
    memcpy(buffer,tmp2+256,blockNumber*512-10);                                       // Last 10 Bytes are not Data Stream Bytes
    free(tmp2);
  }
        
  return 1;
}

int getNicTrackFromPh(int phtrack){
  return phtrack>>2;
}

int getWozTrackFromPh(int phtrack){
   return TMAP[phtrack];
}

int mountNIC(char * filename){
   
  FRESULT fres; 
  FIL fil;  

  fres = f_open(&fil,filename , FA_READ);     // Step 2 Open the file long naming

  if(fres != FR_OK){
    printf("File open Error: (%i)\r\n", fres);
   
    return -1;
  }

  long clusty=fil.obj.sclust;
  int i=0;
  fatCluster[i]=clusty;
  printf("file cluster %d:%ld\n",i,clusty);
  
  while (clusty!=1 && i<30){
    i++;
    clusty=get_fat((FFOBJID*)&fil,clusty);
    printf("file cluster %d:%ld\n",i,clusty);
    fatCluster[i]=clusty;
  }

  mountedImageFile=1;
  f_close(&fil);

  return 0;
}

int mountImagefile(char* path,char * filename){
  int l=0;
  if (filename==NULL)
    return -1;
  char fullp[1024];
  sprintf(fullp,"%s%s",path,filename+2);
  
  printf("Mounting image %s\n",fullp);
  l=strlen(filename);
  if (l>4 && 
      (!memcmp(filename+(l-4),"\x2E\x4E\x49\x43",4)  ||           // .NIC
       !memcmp(filename+(l-4),"\x2E\x6E\x69\x63",4))){            // .nic
     mountWozFile(fullp);
     getSDAddr=getSDAddrNic;
     getTrackBitStream=getNicTrackBitStream;
     getTrackFromPh=getNicTrackFromPh;
  }else if (l>4 && 
      (!memcmp(filename+(l-4),"\x2E\x57\x4F\x5A",4)  ||           // .WOZ
       !memcmp(filename+(l-4),"\x2E\x77\x6F\x7A",4))) {           // .woz
    mountWozFile(fullp);
    getSDAddr=getSDAddrWoz;
    getTrackBitStream=getWozTrackBitStream;
    getTrackFromPh=getWozTrackFromPh;
  }
  return 1;
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
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  uint8_t Test[] = "\n\n**************BOOTING ****************\r\n";                        // Data to send

  HAL_UART_Transmit(&huart1,Test,sizeof(Test),10);             // Sending in normal mode
  HAL_Delay(1000);
  
  EnableTiming();                                                                   // Enable WatchDog to get precise CPU Cycle counting
  initScreen();                                                                     // I2C Screen init

  dirChainedList = list_new();                                                      // ChainedList to manage File list in current path
  currentClistPos=0;                                                                // Current index in the chained List
  lastlistPos=0;                                                                    // Last index in the chained list

  int trk=0; 
                                                                     
  unsigned long t1,t2,diff;
  currentFullPath[0]=0x0;                                                            // Root is ""
  
  fres = f_mount(&fs, "", 1);                                       
  if (fres == FR_OK) {
    walkDir(currentFullPath);
  }else{
    printf("Error mounting sdcard %d\n",fres);
  }

  
  swithPage(FS,NULL);

  csize=fs.csize;
  database=fs.database;
 
  printf("Loading config file");
  loadConfigFile();

  //setConfigFileDefaultValues();
  saveConfigFile();

  cleanJsonMem();
  
  //mountWozFile("BII.NIC");
  
  mountImagefile("","F|DK.woz");
  
  /*
  unsigned char sectorBuf0[8192];
  unsigned char sectorBuf1[8192];
  int rSector=0;
  for (int i=1;i<14;i++){

  
    DWT->CYCCNT = 0;                                                                  // Reset cpu cycle counter
    t1 = DWT->CYCCNT;
    rSector=getSDAddrWoz(trk,0,csize, database);
    cmd18GetDataBlocks(rSector,sectorBuf0,i);

    t2 = DWT->CYCCNT;
    diff = t2 - t1;
    printf("timelapse cmd18GetDatablock %ld cycles\n",diff);
    DWT->CYCCNT = 0;                                                                  // Reset cpu cycle counter
    t1 = DWT->CYCCNT;
    cmd18GetDataBlocksBareMetal(rSector,sectorBuf1,i);
    t2 = DWT->CYCCNT;
    diff = t2 - t1;
    printf("timelapse cmd18GetDatablockBareMetal %ld cycles\n",diff);

    if (memcmp(sectorBuf0,sectorBuf1,i*512)){
      printf("buf does not comp match\n");
      dumpBuf(sectorBuf0,rSector,512);
      dumpBuf(sectorBuf1,rSector,512);
    }else{
      printf("Good i:%d\n",i);
    }
  }
  while(1);
  */

  //dumpBuf(sectorBuf0,rSector,2048);  
  //printf("computed rSector=%d\n",rSector[0]);

  printf("Inside the twilight\n");
  fflush(stdout);


  memset(DMA_BIT_BUFFER,0,sizeof(char)*DMABlockSize);
  srand( time( NULL ) );
    
  unsigned char woz_track_data_bloc[3*DMABlockSize]; 
  
  for (int i=0;i<3;i++){
    woz_sel_trk[i]=-1;
    memset(woz_track_data_bloc,0,sizeof(char)*3*DMABlockSize);
  }

  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_7,GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_8,GPIO_PIN_RESET);

  TIM1->CCR1 = 64;                                                                  // Set the Duty Cycle 
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);                                // Start the PWN to for the Logic Analyzer to decode SPI with Pulse 250 Khz
  
  for (int i=0;i<45;i++)                                                            // Used for Logic Analyzer and sync between SPI and Timer1 PWM
    __NOP();                                                                        // Macro to NOP assembly code
 
  trk=0;
  woz_block_sel_012=0;
  
  getTrackBitStream(0,woz_track_data_bloc);
  woz_sel_trk[0]=0; 

  //dumpBuf(woz_track_data_bloc+woz_block_sel_012*DMABlockSize,1,512);
  memcpy(DMA_BIT_BUFFER,woz_track_data_bloc,DMABlockSize);                      // copy the new track data to the DMA Buffer
        
  HAL_SPI_Transmit_DMA(&hspi1,DMA_BIT_BUFFER,DMABlockSize); 

  // Preload track 1
  getTrackBitStream(1,woz_track_data_bloc+DMABlockSize);
  woz_sel_trk[1]=1;
  //printf("track 1\n");
  //dumpBuf(woz_track_data_bloc+1*DMABlockSize,1,512);
  
  // Preload track 2
  getTrackBitStream(2,woz_track_data_bloc+2*DMABlockSize);
  woz_sel_trk[2]=2; 
 
  int f=0;
  while (1){
  
    /*
    if ((k1+1)%128==0 && k1<1024){
      for (int i=0;i<128;i++){
        char * cstp=byte_to_binary(dbg_stp[i]);
        char * cpstp=byte_to_binary(dbg_prevStp[i]);
        printf("%04d stp:%02d, newStp:%03d, prevStp:%03d,ph_track:%03d %s => %s move:%d \n",i,dbg_stp[i],dbg_newStp[i],dbg_prevStp[i], dbg_phtrk[i],cpstp,cstp,dbg_move[i]);
        free(cstp);
        free(cpstp);
      }
    }*/
    
  
    if (/*!isDiskIIDisable() &&*/ trkChangeFlg==1 && mountedImageFile==1){                                                                  // Track has changed
      trkChangeFlg=0;                                                                // Important to change the flag at the top
      trk=getTrackFromPh(ph_track);                                                  // get the trk according to the file WOZ|NIC
      f=0;                                                                           // flag for main track data found withing the adjacent track
      unsigned int fp1=0,fm1=0;
      DWT->CYCCNT = 0;                                                               // Reset cpu cycle counter
      t1 = DWT->CYCCNT;
      prevTrk=trk;
      HAL_SPI_DMAPause(&hspi1);                                                       // Pause the current DMA
      
      if (trk==255){                                                                  // If Track is 255 then stuf it with random bit 
        whiteNoise=1;                                                                 // Enable repeat random bit in the buffer with DMA half/cmplt buffer interrupt
        for (int i=0;i<DMABlockSize;i++){
          DMA_BIT_BUFFER[i]=rand();
        }
        HAL_SPI_DMAResume(&hspi1);
        t2 = DWT->CYCCNT;
        diff = t2 - t1;
        printf("ph:%02d newTrak:%02d, prevTrak:%02d %ld cycles\n",ph_track,trk,prevTrk,diff);
        continue;
      }
      whiteNoise=0;

      for (int i=0;i<3;i++){
        if (woz_sel_trk[i]==trk){
          f=1;
          woz_block_sel_012=i;
          break;
        }
      }

      if (f!=1){
        woz_block_sel_012=1;
        getTrackBitStream(trk,woz_track_data_bloc+woz_block_sel_012*DMABlockSize);
        woz_sel_trk[1]=trk;       
      }

      memcpy(DMA_BIT_BUFFER,woz_track_data_bloc+woz_block_sel_012*DMABlockSize,DMABlockSize);      // copy the new track data to the DMA Buffer
      HAL_SPI_DMAResume(&hspi1); 
      
      t2 = DWT->CYCCNT;
      diff = t2 - t1;

      DWT->CYCCNT = 0;                                                                            // Reset cpu cycle counter
      t1 = DWT->CYCCNT;

      int selP1=(woz_block_sel_012+1)%3;                                                          // selector number for the track above within 0,1,2
      int selM1=(woz_block_sel_012-1)%3;                                                          // selector number for the track below within 0,1,2

      if (woz_sel_trk[selP1]==(trk+1)){
        fp1=1;
      }
      else if (trk!=35 && woz_sel_trk[selP1]!=(trk+1)){                                           // check if we need to load the track above                                                     
        getTrackBitStream(trk+1,woz_track_data_bloc+selP1*DMABlockSize);     
        woz_sel_trk[selP1]=(trk+1);                                                               // change the trk in the selector
      }

      if (woz_sel_trk[selM1]==(trk-1)){
        fm1=1;
      }
      else if (trk!=0 && woz_sel_trk[selM1]!=(trk-1)){                                            // check if we need to load the track above
        getTrackBitStream(trk-1,woz_track_data_bloc+selM1*DMABlockSize);     
        woz_sel_trk[selM1]=(trk-1);                                                               // change the trk in the selector
      }
      t2 = DWT->CYCCNT;
      diff = t2 - t1;
      printf("ph:%02d newTrak:%02d, prevTrak:%02d, %02d-%02d-%02d %02d %d-%d-%d %ld\n",ph_track,trk,prevTrk,woz_sel_trk[selM1],woz_sel_trk[woz_block_sel_012],woz_sel_trk[selP1],woz_block_sel_012,fm1,f,fp1,diff);
    }

    else if (nextAction!=NONE){                                         // Several action can not be done on Interrupt
      
      switch(nextAction){
        case FSDISP:
          list_destroy(dirChainedList);
          walkDir(currentFullPath);
          currentClistPos=0;
          initFSScreen("");
          updateFSDisplay(1); 
          nextAction=NONE;
          break;
        case IMG_MOUNT:
          mountImagefile(currentFullPath,selItem);
          swithPage(IMAGE,NULL);
          nextAction=NONE;
          break;
        default:
          break;
      }
      
    }
    

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
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_HIGH;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
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
  sConfigOC.Pulse = 0;
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
  htim2.Init.Period = 200;
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

  /* DMA interrupt init */
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

  /*Configure GPIO pins : BTN_ENTR_Pin BTN_UP_Pin BTN_DOWN_Pin */
  GPIO_InitStruct.Pin = BTN_ENTR_Pin|BTN_UP_Pin|BTN_DOWN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : STEP0_Pin STEP1_Pin STEP2_Pin STEP3_Pin */
  GPIO_InitStruct.Pin = STEP0_Pin|STEP1_Pin|STEP2_Pin|STEP3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : BTN_RET_Pin */
  GPIO_InitStruct.Pin = BTN_RET_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(BTN_RET_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : DEVICE_ENABLE_Pin */
  GPIO_InitStruct.Pin = DEVICE_ENABLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(DEVICE_ENABLE_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);

  HAL_NVIC_SetPriority(EXTI3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI3_IRQn);

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
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{

  //printf("startr here 0 %d\n",GPIO_Pin);
  if(GPIO_Pin == GPIO_PIN_0   ||               // Step 0 PB8
      GPIO_Pin == GPIO_PIN_1  ||              // Step 1 PB9
      GPIO_Pin == GPIO_PIN_2  ||              // Step 2 PB10
      GPIO_Pin == GPIO_PIN_3  
     ) {            // Step 3 PB11
    processDiskHeadMove(GPIO_Pin);
   
  }else if ((GPIO_Pin == BTN_RET_Pin  ||      // BTN_RETURN
            GPIO_Pin == BTN_ENTR_Pin  ||      // BTN_ENTER
            GPIO_Pin == BTN_UP_Pin    ||      // BTN_UP
            GPIO_Pin == BTN_DOWN_Pin          // BTN_DOWN
            )&& buttonDebounceState==true){
           
    buttonDebounceState=false;
    HAL_TIM_Base_Start_IT(&htim2);
    processBtnInterrupt(GPIO_Pin);

            }
  
  
   else {
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
