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
    pattern="0b110101011010101010010110"
    pattern="0xD5AA96"
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
    print("   data:"+pattern+" offset:"+repr(last)+" length:"+repr(delta_b_comp)+" mod:"+repr(8-delta%8))
    c=nibble.cut(bits=delta_b_comp,start=last)
    for nib in c:
        #nib.pp()
        findDataFieldSignature(nib)
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
def file2buf(NICname,buffer):
    file = open(NICname, "rb")
    
    indx=0
    # Reading the first three bytes from the binary file
    data = file.read(512)
    
    # Printing data by iterating with while loop
    while data:
        x=0
        #for b in data:
        buffer[indx*512:(indx*512)-1]=data
        #x=x+1
        data = file.read(512)
        indx=indx+1
       
    # Close the binary file
    file.close()
    #print(buffer)


#print(pd.options.display.max_rows) 
k=1
l=0
str=""
sector=[0] * 512
n = 16*35
m = 512
diskSector = [[0] * m] * n

#file2buf(sys.argv[2],diskSector)

filename = sys.argv[1]
df = pd.read_csv(filename,sep = ';')
niceSector=""
pos=0
buffer=bytearray(64000)
pos=0
for i, j in df.iterrows():
    #print(i, j)
    #print(i)
    str=str+repr(j[3])+" "
    sector[l]=int(j[3],16)
    buffer[pos]=int(j[3],16)
    if k%16==0:
        #print(str)
    #    niceSector=niceSector+str+"\n"
        str=""
    #if k%448==0:
    #    print()
    #    print()
    #    print()
    #    l=0
    #    pos=pos+1
    #    print(pos)
        #res=checkExist(sector,diskSector)
        #if res==-1:
        #    print(niceSector)
    #    niceSector=""
    #else:
    #    l=l+1
    k=k+1
    pos=pos+1

#c = BitArray(buffer)




s=Bits(buffer)
print("total bits:"+repr((pos-1)*8))
#print(list(s.findall('0b11111111001111111100111111110011111111001111111100')))
#print(list(s.findall('0xD5AAAD')))
#print(list(s.findall('0xD5AA96')))
#print(s.startswith('0xD5AA96',start=66442))
pattern='0b11111111001111111100111111110011111111001111111100'
last=0
for item in list(s.findall(pattern)):
    
    delta=item-last
    delta_comp_8=delta+(8-delta%8)
        
    if delta>1024:
        print("SyncBytes offset:"+repr(item)+" length:"+repr(delta))
        c=s.cut(bits=delta_comp_8,start=last,end=item+(8-delta%8))
        for nibble in c:
            #nibble.pp(width=120)
            findAddrFieldSignature(nibble)
    #else:
    #    print("oups:"+repr(delta))
    
    #length_1=408*8
    
    #c=s.cut(bits=delta,start=item,end=start_1)
    
    #for nibble in c:
        #print(list(nibble.findall('0xD5AAAD')))
        #nibble.pp(width=120)
    last=item
#c[0].pp(width=120)
    #for nibble in c:
        #nibble.pp('hex',width=120)
#b=np.right_shift(buffer,4)
#print(b);
#myBytes=bytes([0xFF,0xAA,0x55])
str=""
k=0
#for x in range (0,1024):
#    str=str+hex(b[x])+" "
#    if k%16==0 and k!=0:
#        print(str)
        #niceSector=niceSector+str+"\n"
#        str=""
    #print(hex(b[x]))
#    k=k+1
#bits=int.from_bytes(buffer,byteorder="big")
#bits=bits>>1
#print(bits.to_bytes(3,byteorder="big"))

print("END\n")
