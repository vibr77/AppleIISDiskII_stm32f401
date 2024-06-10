
#ifndef disp
#define disp

void updateFSDisplay();

void initScreen();
void initFSScreen(char * path);

void displayStringAtPosition(int x,int y,char * str);
void inverseStringAtPosition(int lineNumber);
void clearLineStringAtPosition(int lineNumber);
void displayFSItem();


#endif