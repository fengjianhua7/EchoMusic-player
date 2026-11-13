from pathlib import Path
import argparse, struct, zlib, hashlib, json
BASE=0x08008000; END=0x0807F800; BOARD=0x10300512

def read_hex(path):
 data={};base=0
 for line in Path(path).read_text().splitlines():
  if not line.startswith(':'):raise ValueError('Invalid HEX record')
  b=bytes.fromhex(line[1:])
  if len(b)!=b[0]+5 or sum(b)%256:raise ValueError('HEX checksum failed')
  kind=b[3];addr=int.from_bytes(b[1:3],'big')
  if kind==4:base=int.from_bytes(b[4:6],'big')<<16
  elif kind==0:
   for i,value in enumerate(b[4:-1]):
    a=base+addr+i
    if a in data and data[a]!=value:raise ValueError('HEX overlap')
    data[a]=value
  elif kind not in (1,3,5):raise ValueError('Unsupported HEX addressing')
 return data

def record(address,kind,data):
 b=bytes([len(data),address>>8,address&255,kind])+data
 return ':'+(b+bytes([-sum(b)&255])).hex().upper()

def write_hex(path,memory):
 lines=[];last=None;addresses=sorted(memory);at=0
 while at<len(addresses):
  a=addresses[at];high=a>>16
  if high!=last:lines.append(record(0,4,high.to_bytes(2,'big')));last=high
  chunk=[]
  while at<len(addresses) and addresses[at]==a+len(chunk) and len(chunk)<16 and addresses[at]>>16==high:
   chunk.append(memory[addresses[at]]);at+=1
  lines.append(record(a&65535,0,bytes(chunk)))
 lines.append(record(0,1,b''));Path(path).write_text('\n'.join(lines)+'\n',encoding='ascii')

def make(app,boot,out):
 out=Path(out);out.mkdir(parents=True,exist_ok=True);m=read_hex(app)
 if not m or min(m)!=BASE or max(m)>=END:raise ValueError('STM32 app must link at 0x08008000 and end before calibration')
 payload=bytes(m.get(a,255) for a in range(BASE,max(m)+1));sp,entry=struct.unpack_from('<II',payload)
 if not (0x20000000<sp<=0x20010000 and not sp%8 and entry&1 and BASE<=entry&~1<BASE+len(payload)):raise ValueError('Invalid STM32 vector table')
 crc=zlib.crc32(payload);head=struct.pack('<8sIIIIII',b'STMOTA26',len(payload),crc,BASE,BOARD,1,27).ljust(64,b'\0')
 package=head+payload;(out/'STM32_v27.stmfw').write_bytes(package);(out/'STM32_v27.app.bin').write_bytes(payload)
 b=read_hex(boot)
 if min(b)!=0x08000000 or max(b)>=BASE:raise ValueError('Bootloader bounds invalid')
 if bytes(b.get(0x08007000+i,255) for i in range(4))!=struct.pack('<I',0x26324F54):raise ValueError('Bootloader identity missing')
 if any(b.get(0x08007800+i,0)!=255 for i in range(4)):raise ValueError('Initial update marker must be explicitly erased')
 if set(b)&set(m):raise ValueError('Boot/application overlap')
 write_hex(out/'STM32_v27_initial_combined.hex',{**b,**m})
 info={'stm32ApplicationBytes':len(payload),'stm32PayloadCRC32':f'{crc:08x}','stm32PackageSHA256':hashlib.sha256(package).hexdigest(),'bootHighestAddress':hex(max(b)),'applicationHighestAddress':hex(max(m)),'calibrationStart':hex(END)}
 (out/'manifest.json').write_text(json.dumps(info,indent=2),encoding='utf-8');return info

if __name__=='__main__':
 here=Path(__file__).resolve().parent
 parser=argparse.ArgumentParser();parser.add_argument('--app',default=here.parent/'STM32_Library_Fix/Project/Objects/BH-F103.hex');parser.add_argument('--boot',default=here.parent/'STM32_OTA_Bootloader/Project/Objects/STM32-Bootloader.hex');parser.add_argument('--out',default=here)
 args=parser.parse_args();print(json.dumps(make(args.app,args.boot,args.out),indent=2))
