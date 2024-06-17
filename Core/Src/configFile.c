#include "configFile.h"
#include "fatfs.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "parson.h"

const char configFilename[]="sdiskConfig.json";
JSON_Value *root_value;
JSON_Object *configParams;

void loadConfigFile(){

    
    root_value = json_parse_file(configFilename);

    configParams = json_value_get_object(root_value);

    for (size_t j=0;j<json_object_get_count(configParams);j++){
        JSON_Value * val=json_object_get_value_at(configParams,j);
        printf("key:%s ",json_object_get_name(configParams,j));
        if (json_value_get_type(val)==JSONNumber)
            printf("value %f\n",json_value_get_number(val));
        else 
            printf("value %s\n",json_value_get_string(val));
    }

    getConfigParamStr(configParams,"currentPath");
    setConfigParamStr(configParams,"currentPath","Zooby");
    getConfigParamStr(configParams,"currentPath");
    
    /* cleanup code */
    //saveConfigFile();
    //cleanJsonMem();
}

void setConfigFileDefaultValues(){
    setConfigParamStr(configParams,"currentPath","");
    setConfigParamInt(configParams,"autoMountLastImage",1);
    setConfigParamInt(configParams,"allowWozFileFormat",1);
    setConfigParamInt(configParams,"allowNicFileFormat",1);
    setConfigParamStr(configParams,"lastImageFile","DK.woz");
}

void saveConfigFile(){
    //JSON_Value * val= json_object_get_wrapping_value();
    json_serialize_to_file(root_value,configFilename);
}

void cleanJsonMem(){
     json_value_free(root_value);
}

const char * getConfigParamStr(JSON_Object * configParams,char * key){
    if (configParams==NULL || key==NULL){
        return NULL;
    }
    const char * ret=json_object_get_string(configParams,key);
    printf("getParam:%s %s\n",key,ret);
    return ret;
}

int getConfigParamInt(JSON_Object* configParams,char * key){
    if (configParams==NULL || key==NULL){
        return -1;
    }
    printf("getParam:%s %f\n",key,json_object_get_number(configParams,key));
}

int setConfigParamStr(JSON_Object* configParams,char * key,char * value){
    if (configParams==NULL || key==NULL || value ==NULL){
        return -1;
    }

    json_object_set_string(configParams,key,value);
    return 1;
}

int setConfigParamInt(JSON_Object* configParams,char * key,int value){
    if (configParams==NULL || key==NULL){
        return -1;
    }

    json_object_set_number(configParams,key,value);
    return 1;
}



