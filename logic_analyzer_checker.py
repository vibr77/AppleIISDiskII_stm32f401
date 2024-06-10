import pandas as pd
import sys

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

file2buf(sys.argv[2],diskSector)

filename = sys.argv[1]
df = pd.read_csv(filename)
niceSector=""
pos=0
for i, j in df.iterrows():
    #print(i, j)
    str=str+j[3]+" "
    sector[l]=int(j[3],16)
    if k%16==0:
        #print(str)
        niceSector=niceSector+str+"\n"
        str=""
    if k%448==0:
        print()
        print()
        print()
        l=0
        pos=pos+1
        print(pos)
        res=checkExist(sector,diskSector)
        if res==-1:
            print(niceSector)
        niceSector=""
    else:
        l=l+1
    k=k+1

