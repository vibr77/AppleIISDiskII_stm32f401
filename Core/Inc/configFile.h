#include "parson.h"
#ifndef CONFIG_FI

#define CONFIG_FI

#define maxLength 512


void loadConfigFile();
void saveConfigFile();
void cleanJsonMem();

void setConfigFileDefaultValues();

const char * getConfigParamStr(JSON_Object * configParams,char * key);
int getConfigParamInt(JSON_Object * configParams,char * key);

int setConfigParamStr(JSON_Object * configParams,char * key,char * value);
int setConfigParamInt(JSON_Object * configParams,char * key,int value);

#endif