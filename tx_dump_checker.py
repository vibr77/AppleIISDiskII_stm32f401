#
#__   _____ ___ ___        Author: Vincent BESSON
# \ \ / /_ _| _ ) _ \      Release: 0.1
#  \ V / | || _ \   /      Date: 2024
#   \_/ |___|___/_|_\      Description: Apple Disk II Emulator data script
#                2024      Licence: Creative Commons
#______________________



# python3 tx_dump_checker.py ./dump/dump_tx_trk0_p1.bin ./imageFiles/BII.nic

import pandas as pd
import numpy as np
from bitstring import Bits, BitArray, BitStream, pack, options
import sys

options.no_color=True
indx=0

def decodeAddr(buffer):
    trk=0
    trackpos=5
    #print(buffer[5:9])
    c1=buffer[trackpos]<<1 | 0b00000001
    c2=buffer[trackpos+1]
    #print("{0:b}".format(c1))
    #print("{0:b}".format(c2))
    trk=c1 & c2 

    sector=0
    c1=buffer[trackpos+2]<<1 | 0b00000001
   
    c2=buffer[trackpos+3]
    sector=c1 & c2 
   
    ret=[]
    ret.append(trk)
    ret.append(sector)
    #print("Track:"+repr(trk)+" sector:"+repr(sector))
    return ret

def findPatternClosing(nibble: Bits,endPattern,startPos,clen):
    
    c=nibble.cut(bits=clen-clen%8,start=startPos)
    for nib in c:
        #nib.pp()
        endLst=list(nib.findall(endPattern))
        endItem=0
        for endItem in endLst:
            break
        length=endItem+len(endPattern)*8
        return length


def findPattern(nibble: Bits,pattern,endPattern,absOffset):
    
    lst=list(nibble.findall(pattern))
    last=0
    indx=0
    for item in lst:
        #print("Item:"+repr(item)+" last:"+repr(last))
        delta=item-last
        if nibble[last:last+24].tobytes()!=b'\xd5\xaa\x96':                                         # If Signature is not present first part of the segment
                print("trailling of the sector at the end of the disk s:"+repr(last)+" end:"+repr(item)+" "+hex(int(item/8))+".H length:"+repr(delta)+".b "+repr(delta/8)+".B bit shift:"+repr(item%8))             
        else:
            if pattern=="0xD5AA96":
                addr=nibble[last:last+112].tobytes()
                ret=decodeAddr(addr)
            length=findPatternClosing(nibble,endPattern,last,delta)
            s_newOffset=absOffset+last
            e_newOffset=absOffset+item
            if pattern=="0xD5AA96":
                print("Indx:{0:02d}".format(indx)+" pattern:"+pattern+"  Addr: "+" ".join('{:02X}'.format(x) for x in addr[0:9])+" Track:{0:02d}".format(ret[0])+" sector:{0:02d}".format(ret[1])+" s_offset:{0:05d}".format(s_newOffset)+".b {0:05d}".format(int(s_newOffset/8))+".B {0:04X}".format(int(s_newOffset/8))+".H e_offset:{0:05d}".format(e_newOffset)+".b {0:05d}".format(int(e_newOffset/8))+".B {0:04X}".format(int(e_newOffset/8))+".H length:{0:03d}".format(length)+".b "+repr(length/8)+".B shift:"+repr(item%8)) 
            else:
                print("   Indx:{0:02d}".format(indx)+" pattern:"+pattern+" s_offset:{0:05d}".format(newOffset)+".b {0:05d}".format(int(newOffset/8))+".B "+hex(int(newOffset/8))+".H length:{0:03d}".format(length)+".b "+repr(length/8)+".B")
            
        last=item
        indx=indx+1
    l=nibble.length-last
    c=nibble.cut(bits=l-l%8,start=last)                             # This is the remaining part 
    for nibEnd in c:
        #nibEnd.pp()                                                 # Print out the full segment D5.AA.96
        if pattern=="0xD5AA96":
            addr=nibble[last:last+112].tobytes()
            ret=decodeAddr(addr)
        break

    length=findPatternClosing(nibble,endPattern,last,l)
    s_newOffset=absOffset+item
    if pattern=="0xD5AA96":
        print("Indx:{0:02d}".format(indx)+" pattern:"+pattern+"  Addr: "+" ".join('{:02X}'.format(x) for x in addr[0:9])+" Track:{0:02d}".format(ret[0])+" sector:{0:02d}".format(ret[1])+" s_offset:{0:05d}".format(s_newOffset)+".b {0:05d}".format(int(s_newOffset/8))+".B {0:04X}".format(int(s_newOffset/8))+".H length:{0:03d}".format(length)+".b "+repr(length/8)+".B shift:"+repr(item%8))
    else:
        print("   Indx:{0:02d}".format(indx)+" pattern:"+pattern+" s_offset:{0:05d}".format(s_newOffset)+".b {0:05d}".format(int(s_newOffset/8))+".B "+hex(int(s_newOffset/8))+".H length:{0:03d}".format(length)+".b "+repr(length/8)+".B")
         



def findAddrFieldSignature(nibble: Bits,pattern,endPattern,absOffset,ref):
    
    lst=list(nibble.findall(pattern))
    last=0
    indx=0
    for item in lst:
        delta=item-last
        if nibble[last:last+24].tobytes()!=b'\xd5\xaa\x96':                                         # If Signature is not present first part of the segment
            print("trailling of the sector at the end of the disk s:"+repr(last)+" end:"+repr(item)+" "+hex(int(item/8))+".H length:"+repr(delta)+".b "+repr(delta/8)+".B bit shift:"+repr(item%8))             
        else:            
            addr=nibble[last:last+112].tobytes()
            ret=decodeAddr(addr)
            
            c=nibble.cut(bits=delta,start=last,end=item)
            for nib in c:
                endLst=list(nib.findall(endPattern))
                endItem=0
                for endItem in endLst:
                    break
        
            if c:

                length=endItem+len(endPattern)*8
                newOffset=absOffset+item
                print("_________________________________________________________________________________________________")
            
                print("Indx:{0:02d}".format(indx)+" pattern:"+pattern+" Addr: "+" ".join('{:02X}'.format(x) for x in addr[0:9])+" Track:{0:02d}".format(ret[0])+" sector:{0:02d}".format(ret[1])+" offset:{0:05d}".format(newOffset)+".b {0:05d}".format(int(newOffset/8))+".B {0:04X}".format(int(newOffset/8))+".H length:{0:03d}".format(length)+".b "+repr(length/8)+".B")
                findDataFieldSignature(nib,"0xD5AAAD","0xDEAAEB",newOffset,ret[1],ref)
            
        last=item
        indx=indx+1
    
    # do not forget the remaining piece 
    l=nibble.length-last
    c=nibble.cut(bits=l-l%8,start=last)                             # This is the remaining part 
    for nib in c:
        addr=nibble[last:last+112].tobytes()
        ret=decodeAddr(addr)
        endLst=list(nib.findall(endPattern))
        endItem=0
        for endItem in endLst:
            break
        break
    if not endItem:
        length=endItem+len(endPattern)*8
        newOffset=absOffset+item
        newOffetEnd=absOffset+item+length
        print("_________________________________________________________________________________________________")
        
        print("Indx:{0:02d}".format(indx)+" pattern:"+pattern+" Addr: "+" ".join('{:02X}'.format(x) for x in addr[0:9])+" Track:{0:02d}".format(ret[0])+" sector:{0:02d}".format(ret[1])+" offset:{0:05d}".format(newOffset)+".b {0:05d}".format(int(newOffset/8))+".B {0:04X}".format(int(newOffset/8))+".H end:"+hex(int(newOffetEnd/8))+".H length:{0:03d}".format(length)+".b "+repr(int(length/8))+".B")
        findDataFieldSignature(nib,"0xD5AAAD","0xDEAAEB",newOffset,ret[1],ref)
        
    print()


def findDataFieldSignature(nibble: Bits,pattern,endPattern,absOffset,sector,ref):
    
    lst=list(nibble.findall(pattern))
    
    last=0
    indx=0
    for item in lst:
        delta=nibble.length-item
        c=nibble.cut(bits=delta-delta%8,start=item)
        len1=nibble.length-delta
        for nib in c:
            
            endLst=list(nib.findall(endPattern))
            endItem=0
            lastItem=0
            
            for endItem in endLst:
                c=nib.cut(endItem+3*8,start=0) 
                for nib2 in c: 
                    if ref==0:
                        data_src.append(nib2)
                        indx_src.append(sector)
                    else:
                        data_ref.append(nib2)
                        indx_ref.append(sector)
                    #nib2.pp()
                    break
                break
            break
        newOffset=absOffset+item
        if not endLst:
            print("   Indx:{0:02d}".format(indx)+" pattern:"+pattern+" WARNING missing closing pattern offset:{0:05d}".format(newOffset)+".b {0:05d}".format(int(newOffset/8))+".B "+hex(int(newOffset/8))+".H ") 
        else:
            length=nib2.length
            newOffetEnd=absOffset+item+length
        
            print("   Indx:{0:02d}".format(indx)+" pattern:"+pattern+" offset:{0:05d}".format(newOffset)+".b {0:05d}".format(int(newOffset/8))+".B "+hex(int(newOffset/8))+".H end:"+hex(int(newOffetEnd/8))+".H length:{0:03d}".format(length)+".b "+repr(int(length/8))+".B")
        print()
        #nib.pp(width=200)    
        last=item
        indx=indx+1
        break       
    #print()



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

def file2buf(NICname,buf):
    file = open(NICname, "rb")
    
    indx=0
    # Reading the first three bytes from the binary file

    data = file.read(512)
    
    while data:
        buf[indx*512:(indx*512)-1]=data
        data = file.read(512)
        indx=indx+1
        if indx>16:
            break


def woz2buf(WOZname,buf,track):
    file = open(WOZname, "rb")
    file.seek(1536+track*6656)
    indx=0
    # Reading the first three bytes from the binary file
    data = file.read(512)
    
    while data:
        buf[indx*512:(indx*512)-1]=data
        data = file.read(512)
        indx=indx+1
        if indx>12:
            break

def nic2buf(NICname,buf,track):
    
    file = open(NICname, "rb")
    file.seek(track*512*16)
    indx=0
    # Reading the first three bytes from the binary file
    data = file.read(512)
    
    while data:
        buf[indx*512:(indx*512)-1]=data
        data = file.read(512)
        indx=indx+1
        if indx>16:
            break



def csv2buf(filename,buf: bytearray):
    
    df = pd.read_csv(filename,sep = ';')
    pos=0
    for i, j in df.iterrows():
        #print(i, j)
        #str=str+repr(j[3])+" "
        #print(j[3])
        buf.append(int(j[3],16))
        pos=pos+1
    
    filew=file = open(filename+".bin", "wb")
    filew.write(buffer)
    filew.close
    
    return buf

indx_src=[]
data_src=[]
arr_indx_src=[]

indx_ref=[]
data_ref=[]
arr_indx_ref=[]

buffer=bytearray()

#buffer=csv2buf(sys.argv[1],buffer)
woz2buf(sys.argv[1],buffer,1)
#file2buf(sys.argv[1],buffer)
s=Bits(buffer)
#s.pp()

bufferRef=bytearray(64000)
nic2buf(sys.argv[2],bufferRef,1)
sRef=Bits(bufferRef)


pos=indx*512

print("Total bits:"+repr((pos-1)*8))

#pattern='0b11111111001111111100111111110011111111001111111100'

pattern="0xD5AA96"

last=0
indx=0

findPattern(s,"0xD5AA96","0xDEAAEB",0)


'''
findAddrFieldSignature(s,"0xD5AA96","0xDEAAEB",0,0)

print("----------------------------------------------------------")
print("$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$")
print("----------------------------------------------------------")


findAddrFieldSignature(sRef,"0xD5AA96","0xDEAAEB",0,1)

print("**********************")
print("START COMPARE")
print("**********************")

for i in indx_ref:
    sector_found=0
    inx=0
    for j in indx_src:
        
        if j==i:
            sector_found=1
            
            c1=data_src[inx]
            c2=data_ref[i]
            
            match=1
            
            for ii in range(0,c1.length):
                if c1[ii]!=c2[ii]:
                    match=0
                    break

            if match==1:
                print("found matching sector:"+repr(i)+" src pos:"+repr(inx)+" ref pos:"+repr(i))
            else:
                print("found Non matching sector:"+repr(i)+" first error in src pos:"+repr(ii/8))
                #print("SRC:")
                #c1.pp(width=200)
                #print("REF:")
                #c2.pp(width=200) 

            break
        inx=inx+1
    if sector_found==0:
        print("Warning sector:"+repr(i)+" was not found")

print()
for i in indx_src:
    print(i)
'''
print("END\n")
