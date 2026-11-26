"""Validate generated CIA integrity AND basic Old 3DS launch parameters.
This is a static check, not a hardware boot test.
"""
from pathlib import Path
import hashlib,struct,sys

def require(value,message):
 if not value:raise ValueError(message)
def inspect(data):
 require(len(data)>=32,'CIA header is truncated')
 header,kind,version,cert,ticket,tmd,meta,content=struct.unpack_from('<IHHIIIIQ',data)
 align=lambda n:(n+63)&~63
 tmd_off=align(align(align(header)+cert)+ticket);content_off=align(tmd_off+tmd)
 require(content_off+content<=len(data),'CIA content extends past EOF')
 require(data[content_off+0x100:content_off+0x104]==b'NCCH','Missing NCCH header')
 program=struct.unpack_from('<Q',data,content_off+0x118)[0]
 require(program==0x000400000F4D5300,'Unexpected Mindustry 3DS program ID')
 ex=data[content_off+0x200:content_off+0xa00]
 require(len(ex)==0x800,'Truncated exheader')
 require(hashlib.sha256(ex[:0x400]).digest()==data[content_off+0x160:content_off+0x180],'Exheader hash mismatch')
 chunk=tmd_off+0x140+0xc4+64*0x24
 require(chunk+48<=len(data),'Truncated TMD content record')
 length=struct.unpack_from('>Q',data,chunk+8)[0]
 require(length==content,'Unexpected TMD content length')
 require(hashlib.sha256(data[content_off:content_off+length]).digest()==data[chunk+16:chunk+48],'TMD content hash mismatch')
 priority=ex[0x20f]
 require(0x18<=priority<=0x3f,f'Invalid application thread priority: {priority}; expected 24..63')
 require(priority>=ex[0x60f],'Thread priority exceeds the access descriptor privilege')
 require(struct.unpack_from('<Q',ex,0x200)[0]==program,'Program ID differs between NCCH and exheader')
 flags=ex[0x20e];processor=flags&3;affinity=(flags>>2)&3;mode=flags>>4
 require(affinity and affinity&(1<<processor),'Ideal processor is outside the affinity mask')
 require(mode==0,'Not an Old 3DS 64 MB application')
 require(ex[0x20d]&15==0,'Unexpected New 3DS extended memory mode')
 stack=struct.unpack_from('<I',ex,0x1c)[0]
 require(stack>=0x20000 and stack%4096==0,'Insufficient or unaligned main-thread stack')
 text_addr,text_pages,text_size=struct.unpack_from('<III',ex,0x10)
 require(text_addr==0x100000 and text_pages and text_size,'Invalid executable code segment')
 title_version=struct.unpack_from('>H',data,tmd_off+0x140+0x9c)[0]
 return {'title_version':title_version,'program_id':f'{program:016X}','thread_priority':priority,'stack_size':stack,'memory_mode':'64MB / Legacy'}
if __name__=='__main__':
 try:
  details=inspect(Path(sys.argv[1] if len(sys.argv)>1 else 'mindustry-3ds.cia').read_bytes())
  print('PASS CIA integrity and launch parameters:',details)
 except (ValueError,struct.error,OSError) as exc:
  print('CIA VALIDATION FAILED:',exc,file=sys.stderr);sys.exit(1)
