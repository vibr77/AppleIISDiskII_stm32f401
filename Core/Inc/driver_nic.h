#ifndef nic_h
#define nic_h

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int getNicTrackFromPh(int phtrack);
long getSDAddrNic(int trk,int block,int csize, long database);
enum STATUS getNicTrackBitStream(int trk,unsigned char * buffer);
enum STATUS mountNicFile(char * filename);

#endif