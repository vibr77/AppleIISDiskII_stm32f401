#include "stdio.h"
#include "display.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "list.h"
#include "main.h"

extern list_t * dirChainedList;
extern int nextClistPos;
extern int lastlistPos;
extern char currentFullPath[1024]; 

extern int currentClistPos;

int dispIndexPos; 

int FSMAXLINE=4;                                // Max FS Item line to be displayed

void updateFSDisplay(int init){
  int offset=5;
  int h_offset=10;
  int dispFlag=0;
  char tmp[32];
  char * value;
  list_node_t *pItem[FSMAXLINE];
  for (int i=0;i<FSMAXLINE;i++){
    pItem[i]=NULL;
  }
  
  if (init==1){
    for (int i=0;i<FSMAXLINE;i++){
          dispIndexPos=0;
          pItem[i]=list_at(dirChainedList, currentClistPos+i);
          dispFlag=1;
    }
  }
  else if (dispIndexPos<(FSMAXLINE-1) && nextClistPos==(currentClistPos+1)){     // Case Select Item is changed but not the list of items
      inverseStringAtPosition(1+dispIndexPos,offset);
      dispIndexPos++;
      currentClistPos=nextClistPos;
  }
  else if (dispIndexPos==(FSMAXLINE-1) && nextClistPos==(currentClistPos+1)){     // last item we refresh the list of item and keep last row as selected item
    currentClistPos=nextClistPos;
    //printf("current:%d %d\n",currentClistPos,currentClistPos-(FSMAXLINE-1));

    for (int i=0;i<FSMAXLINE;i++){
      pItem[i]=list_at(dirChainedList, currentClistPos-(FSMAXLINE-1)+i);      
    }
    dispFlag=1;
  }
  else if (dispIndexPos>0 && nextClistPos==(currentClistPos-1)){     // Cursor position change, not the list of item
    inverseStringAtPosition(1+dispIndexPos,offset);
    dispIndexPos--;
    currentClistPos=nextClistPos;
  }

  else if (dispIndexPos==0 && nextClistPos==(currentClistPos-1)){     // New list of item has to be displayed cursor position stays the same
    currentClistPos=nextClistPos;
    for (int i=0;i<FSMAXLINE;i++){
      pItem[i]=list_at(dirChainedList, currentClistPos+i);      
    }
    dispFlag=1;
  }

  else if (dispIndexPos==(FSMAXLINE-1) && nextClistPos==0){                        // roll to the start of the list
    inverseStringAtPosition(1+dispIndexPos,offset);
    dispIndexPos=0;
    currentClistPos=nextClistPos;
    for (int i=0;i<FSMAXLINE;i++){
      pItem[i]=list_at(dirChainedList, currentClistPos+i);      
    }
    dispFlag=1;
  }

   else if (dispIndexPos==0 && nextClistPos==(lastlistPos-1)){             // roll to the end of the list
    inverseStringAtPosition(1+dispIndexPos,offset);
    dispIndexPos=2;
    currentClistPos=nextClistPos;
    for (int i=0;i<FSMAXLINE;i++){
      pItem[i]=list_at(dirChainedList, i-(FSMAXLINE-1));      
    }
    dispFlag=1;
  }

  if (dispFlag){
    for (int i=0;i<FSMAXLINE;i++){

      clearLineStringAtPosition(1+i,offset);
      if (pItem[i]){
        value=pItem[i]->val;
        if (value[0]=='D' && value[1]=='|'){
            dispIcon(1,(1+i)*9+offset,0);
            
          
        }else{
          dispIcon(1,(1+i)*9+offset,1);
          //snprintf(tmp,24,"%s",value+2);
        }
          
        snprintf(tmp,24,"%s",value+2);
        
        displayStringAtPosition(1+h_offset,(1+i)*9+offset,tmp);
        
      }
    }
    
  }
  
  sprintf(tmp,"%02d/%02d",currentClistPos+1,lastlistPos);
  displayStringAtPosition(96,6*9+2,tmp);
  inverseStringAtPosition(1+dispIndexPos,offset);

  ssd1306_UpdateScreen();  

}

void dispIcon(int x,int y,int indx){
  const unsigned char icon_set[]  = {
    // 'folder, 8x8px indx=0
  0x00, 0x70, 0x7e, 0x7e, 0x7e, 0x7e, 0x7e, 0x00,
  //  File, 8x8px indx=1
  0x00, 0x7c, 0x54, 0x4c, 0x44, 0x44, 0x7c, 0x00
  };

  ssd1306_DrawBitmap(x,y,icon_set+8*indx,8,8,White);

}

void mountImageScreen(char * filename){
  clearScreen();
  displayStringAtPosition(5,1*9,"Mount image file:");
  displayStringAtPosition(5,2*9,filename);
  displayStringAtPosition(30,4*9,"YES");
  displayStringAtPosition(30,5*9,"NO");
  ssd1306_InvertRectangle(30-5,4*9-1,70,4*9+7);
  ssd1306_UpdateScreen();
}

void toggleMountOption(int i){
  
  ssd1306_InvertRectangle(30-5,4*9-1,70,4*9+7);
  ssd1306_InvertRectangle(30-5,5*9-1,70,5*9+7);
 
  ssd1306_UpdateScreen();
}

void clearScreen(){
  ssd1306_FillRectangle(0,0,127,63,Black);
  ssd1306_UpdateScreen();
  
}

void initScreen(){
    ssd1306_Init();                                               // Get something on the screen
    ssd1306_SetDisplayOn(1);
    displayStringAtPosition(30,3*9,"SmartDiskII");
    ssd1306_UpdateScreen();
}


void initIMAGEScreen(char * imageName,int type){
  
  char tmp[32];
  clearScreen();

  sprintf("Image:%s",imageName);
  displayStringAtPosition(5,1*9,tmp);
  if (type==0)
    displayStringAtPosition(5,2*9,"type: WOZ");
  else 
    displayStringAtPosition(5,2*9,"type: NIC");
  displayStringAtPosition(5,3*9,"Track 0");
  ssd1306_UpdateScreen();
}

void initFSScreen(char * path){

  ssd1306_FillRectangle(0,0,127,63,Black);                // Erase current screen  
  displayStringAtPosition(0,0,"File listing");
  ssd1306_Line(0,8,127,8,White);
  ssd1306_Line(0,6*9,127,6*9,White);
  displayStringAtPosition(0,6*9,path); 
  char tmp[32];
  sprintf(tmp,"%02d/%02d",currentClistPos+1,lastlistPos);
  displayStringAtPosition(96,6*9+2,tmp);

  ssd1306_UpdateScreen();
}

void displayFSItem(){
    //list_node_t *pItem[FSMAXLINE];
    updateFSDisplay(1);
    return;
    int offset=5;
    list_node_t *pItem;
    dispIndexPos=0;
    currentClistPos=0;
    char tmp[32];
    char * value;
    for (int i=0;i<FSMAXLINE;i++){
        pItem=list_at(dirChainedList, currentClistPos+i);
        if (pItem!=NULL){
          value=pItem->val;

          if (value[0]=='D' && value[1]=='|')
          snprintf(tmp,24,"./%s",value+2);
          else
          snprintf(tmp,24,"%s",value+2);

          displayStringAtPosition(1,(i+1)*9+offset,tmp); 
        }
    }
    /*pItem[0]=list_at(dirChainedList, currentClistPos);
    pItem[1]=list_at(dirChainedList, currentClistPos+1);
    pItem[2]=list_at(dirChainedList, currentClistPos+2);
    
    displayStringAtPosition(1,2*9,pItem[0]->val+1);       // +2 
    displayStringAtPosition(1,3*9,pItem[1]->val+1);
    displayStringAtPosition(1,4*9,pItem[2]->val+1);
   */
    inverseStringAtPosition(1+dispIndexPos,offset);

  ssd1306_UpdateScreen();  
}

void displayStringAtPosition(int x,int y,char * str){
  ssd1306_SetCursor(x,y);
  ssd1306_WriteString(str,Font_6x8,White);
}

void inverseStringAtPosition(int lineNumber,int offset){
  ssd1306_InvertRectangle(0,lineNumber*9-1+offset,127,lineNumber*9+7+offset);
}

void clearLineStringAtPosition(int lineNumber,int offset){
   ssd1306_FillRectangle(0,lineNumber*9+offset,127,lineNumber*9+8+offset,Black);
}