#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
enum pattern {ADDR,END,DATA};

unsigned char * findPattern(unsigned char * pBuf,enum pattern type,unsigned int size, unsigned int * shift){
   
    unsigned const char arr[3][8][2]={
        {{0xD5,0xAA},{0xAB,0x55},{0x56,0xAA},{0xAD,0x54},{0x5A,0xA9},{0xB5,0x52},{0x6A,0xA5},{0xD5,0x4B}},
        {{0xDE,0xAA},{0xBD,0x55},{0x7A,0xAB},{0xFA,0x57},{0xEA,0xAE},{0xD5,0x5D},{0xAA,0xBB},{0x55,0x76}},
        {{0xD5,0xAA},{0xAB,0x55},{0x56,0xAA},{0xAD,0x55},{0x5A,0xAA},{0xB5,0x55},{0x6A,0xAB},{0xD5,0x56}}
    };
    
    unsigned const char signature[3][3]={
        {0xD5,0xAA,0x96},
        {0xDE,0xAA,0xEB},
        {0xD5,0xAA,0xAD}
    };
 
    unsigned long offset=0;
    unsigned char * found;

    unsigned char * p=pBuf;
    
    for (unsigned int i=0;i<8;i++){
        p=pBuf;
        found=memchr(p,(unsigned char)arr[type][i][0],size);

        while (found!=NULL){
            offset=(unsigned char*)found-pBuf;
            
            if (size-offset<3)
                return NULL;
            
            if ((unsigned char)pBuf[offset+1]==arr[type][i][1]){

                printf("Found %X.%X\n",pBuf[offset],pBuf[offset+1]);

                if (i==0){
                    *shift=i;
                    return p+offset+2+1;
                }

                unsigned int c=((pBuf[offset-1] ^ (signature[type][0] >> (8-i) )) & /*0b00001111*/ (0xFF >> (8-i)));

                printf("%X offset %ld %X shift:%d buf-1:%X\n",c,offset,offset,i,pBuf[offset-1]);

                if (c== 0){ 
                    c=(pBuf[offset+2] ^ (signature[type][2]<< i)) & /*0b11110000*/((0xFF<<i)& 0xFF);
                    if (c == 0 ){
                        printf("found %X.%X => %X.%X.%X at %04ld shift:%d\n",   arr[type][i][0],
                                                                                arr[type][i][1],
                                                                                signature[type][0],
                                                                                signature[type][1],
                                                                                signature[type][2],
                                                                                offset,i);
                        *shift=i;
                        return p+offset+2+1;
                    }
                }
            }
            found=memchr(p+offset+1,(int)arr[type][i][0],size-offset-1);
        }  
    }
    return NULL;
 }
 
 
 int main () {
  FILE * pFile;
  long lSize;
  unsigned char * buffer;
  size_t result;

  pFile = fopen ( "dump_rx_trk2_p2.bin" , "rb" );
  
  if(pFile==NULL){
   fputs ("File error",stderr);
   exit (1);
  }

  fseek (pFile , 0 , SEEK_END);
  lSize = ftell (pFile);
  rewind (pFile);

  printf("\n\nFile is %ld bytes \n\n", lSize);

  buffer = (unsigned char*) malloc (sizeof(char)*lSize);
  if(buffer == NULL){
   fputs("Memory error",stderr);
   exit (2);
  }

  result = fread (buffer,1,lSize,pFile);
  if(result != lSize){
   fputs("Reading error",stderr);
   exit (3);
  }


  unsigned char * buffer2=(unsigned char *)malloc(512*sizeof(char));
  memcpy(buffer2,buffer+0x4D0,512);

  unsigned int iret=0;
  unsigned int sze=6656;

  unsigned char *p=findPattern(buffer,ADDR,sze,&iret);
  
  if (p!=NULL){
    p=findPattern(p,ADDR,sze-(p-buffer),&iret);
  }else 
    printf("p is null\n");

/*
if (p!=NULL){
    p=findPattern(p,DATA,sze-(p-buffer),&iret);
}

if (p!=NULL){
   p=findPattern(p,END,sze-(p-buffer),&iret);
}

if (p!=NULL)
    printf("offset %ld",p-buffer);    
else
    printf("not found");
*/
printf("END\n");


  //Logic to check for hex/binary/dec

  fclose (pFile);
  free (buffer);
  return 0;
 }