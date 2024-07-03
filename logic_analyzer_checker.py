#
#__   _____ ___ ___        Author: Vincent BESSON
# \ \ / /_ _| _ ) _ \      Release: 0.1
#  \ V / | || _ \   /      Date: 2024
#   \_/ |___|___/_|_\      Description: Apple Disk II Emulator data script
#                2024      Licence: Creative Commons
#______________________

import pandas as pd
import numpy as np
from bitstring import Bits, BitArray, BitStream, pack, options
import sys

options.no_color=True




def findDataFieldSignature(nibble: Bits):
    pattern="0xD5AAAD"
    lst=list(nibble.findall(pattern))
    last=0
    
    for item in lst:
        delta=item-last
        delta_comp_8=delta+(8-delta%8)
        #print("   data:"+pattern+" offset:"+repr(item)+" length:"+repr(delta)+" mod:"+repr(8-delta%8))
        c=nibble.cut(bits=delta_comp_8,start=last,end=item+(8-delta%8))
        last=item
    delta=nibble.length-last
    delta_b_comp=delta-delta%8
    if delta_b_comp>0:
        print("   data:"+pattern+" offset:"+repr(last)+" length:"+repr(delta_b_comp)+" mod:"+repr(8-delta%8))
        c=nibble.cut(bits=delta_b_comp,start=last)
        for nib in c:
            nib.pp()


def findAddrFieldSignature(nibble: Bits):
    
    pattern="0xD5AA96"
    lst=list(nibble.findall(pattern))
    print(nibble.length)
    last=0
    
    for item in lst:
        delta=item-last
        delta_comp_8=delta-delta%8
        print("   data:"+pattern+" offset:"+repr(item)+" length:"+repr(delta)+" mod:"+repr(8-delta%8))
        c=nibble.cut(bits=delta_comp_8,start=last,end=item-delta%8)
        #for nib in c:
        #    nib.pp()

        last=item
    #delta=nibble.length-last
    #delta_b_comp=delta-delta%8
    #print("   data:"+pattern+" offset:"+repr(last)+" length:"+repr(delta_b_comp)+" mod:"+repr(8-delta%8))
    #c=nibble.cut(bits=delta_b_comp,start=last)
    #for nib in c:
    #    nib.pp()
        #findDataFieldSignature(nib)
    #print("  Data 0xD5AA96 offset:"+repr(item)+" length:"+repr(delta))

    #print(list(nibble.findall('0xD5AA96')))

def checkExist(buffer,fileBuffer):
    #print("Buffer0:"+hex(buffer[0]))
    #buffer=[0xFF,0xFF]
    #if buffer[0:1]==fileBuffer[0:1]:
    #    print("Yeah")
    #else:
    #    print("NO")
    #    print(hex(fileBuffer[0]))
    for x in range (0,560):
        if fileBuffer[x*512:(x+1)*512-1]==buffer[0:511]:
            print("OK Dirdel Sector:")
            print(x)
            return x
            break
    return -1

#
# Open file and create a byte buffer
#
def file2buf(NICname,dstBuffer):
    file = open(NICname, "rb")
    
    indx=0
    data = file.read(512)
    
    while data:
        dstBuffer[indx*512:(indx*512)-1]=data
        data = file.read(512)
        indx=indx+1
       
    # Close the binary file
    file.close()


def processPattern(data:Bits,comp:Bits):
    start=0
    chunk=256
    ex_chunk=8
    outcome=[0]*8000
    while start+chunk < 6656:
        print("search chunk start:"+repr(start)+" chunk:"+repr(chunk))
        flgFound=0
        pattern=comp[start*8:start*8+chunk]
        lst=data.findall(pattern)
        for item in lst:
            print("   found:"+repr(item))
            flgFound=1
        if flgFound==1:
            while flgFound==1:
                flgFound=0
                chunk=chunk+ex_chunk
                pattern=comp[start*8:start*8+chunk]
                lst=data.findall(pattern)
                for item in lst:
                    print("      extend chunk:"+repr(chunk+ex_chunk))
                    flgFound=1
                    pos=item
                
                if flgFound==0:
                    for i in range (start,start+chunk-ex_chunk):
                        outcome[i]=pos


                    print("      extend exit:"+repr(start+chunk)+" chunk:"+repr(chunk))
            start=start+chunk
            chunk=512
        else:
            print("not found")
            for i in range (start,start+chunk):
                outcome[i]=0
            start=start+ex_chunk
    myiter = iter(range(0, 6656))
    subsize=0
    tsize=0
    for i in myiter:
        if outcome[i]==0:
            k=i
            while outcome[i]==0:
                i=i+1
                next(myiter, None)
                if i>6656:
                    break
            subsize=i-k
            tsize=tsize+subsize   
            print("not found chunk:"+repr(k)+"->"+repr(i)+" subsize:"+repr(subsize)+" tsize:"+repr(tsize))


#
# Decode the D5AA96 Address
#

def decodeAddr(buffer):
    trk=0
    c=buffer[5] & 0b01010101
    trk=(c<<1) & 0b100000000 | (c<<2 & 0b01000000) | (c << 3 & 0b00100000) | (c<<4 & 0b00010000)
    c=buffer[6] 
    trk|=(c & 0b00000001) | (c>> 1 & 0b00000010) | (c>> 2 & 0b00000100) | (c >> 3 & 0b00001000)
    print("track:"+repr(trk))
    print("{0:b}".format(buffer[5]))
    print("{0:b}".format(buffer[6]))
    print("{0:b}".format(trk))

    sector=0
    c=buffer[7] & 0b01010101
    sector=(c<<1) & 0b100000000 | (c<<2 & 0b01000000) | (c << 3 & 0b00100000) | (c<<4 & 0b00010000)
    c=buffer[8] 
    sector|=(c & 0b00000001) | (c>> 1 & 0b00000010) | (c>> 2 & 0b00000100) | (c >> 3 & 0b00001000)
    print("sector:"+repr(sector))
    print("{0:b}".format(buffer[7]))
    print("{0:b}".format(buffer[8]))
    print("{0:b}".format(sector))

def processBuffer(s,pattern):
    #pattern='0b11111111001111111100111111110011111111001111111100'
    print("search bit pattern:"+pattern)
    #pattern="0xDEAAEB"
    last=0
    indx=0
    for item in list(s.findall(pattern,bytealigned=False)):
        
        delta=item-last
        delta_comp_8=delta-delta%8
           
        if delta!=0:
            print("   indx:"+repr(indx)+" offset:"+repr(item)+" (bits)  length:"+repr(delta)+" bits bytes:"+repr(delta/8))
            #print(s[item:item+112])
            s[item:item+112].pp()
            #print("Prologue: "+repr(s[item+11*8:item+14*8].tobytes()))
            #s[item+11*8:item+14*8].pp()
            #c=s.cut(bits=delta_comp_8,start=last,end=item-(delta%8))
            #for nibble in c:
                #nibble.pp(width=120)
                #findAddrFieldSignature(nibble)
            indx=indx+1
        last=item

#print(pd.options.display.max_rows) 
k=1
l=0
str=""
sector=[0] * 512
n = 16*35
m = 512




#filename = sys.argv[1]
#df = pd.read_csv(filename,sep = ';')

#pos=0
#buffer=bytearray(64000)
#buffer2=bytearray(64000)

#pos=0
#for i, j in df.iterrows():
    #print(i, j)
#    str=str+repr(j[3])+" "

#    buffer[pos]=int(j[3],16)
#    if k%16==0:
        #print(str)
    #    niceSector=niceSector+str+"\n"
#        str=""
    
#    k=k+1
#    pos=pos+1

#xlBuffer=Bits(buffer)

#file2buf(sys.argv[2],buffer2)
#fs=Bits(buffer2)
#print(fs.length)

#print("total bits:"+repr((pos-1)*8))
bArr=b'\xD5\xAA\x96\xFF\xFE\xAB\xAB\xAA\xAE'
decodeAddr(bArr)
#processPattern(fs,xlBuffer)
#print("from RX_DMA:")
#processBuffer(fs,"0xD5AA96")
#processBuffer(fs,"0xDEAAEB")
#print()
#processBuffer(xlBuffer,"0xD5AA96")
#processBuffer(xlBuffer,"0xDEAAEB")
#print("Logic Analyzer:")
#processBuffer(xlBuffer)




