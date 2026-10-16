#!/usr/bin/env python3
"""Read-only decoder for Daikin D-Checker 3 .ldd files (raw DEFLATE + NRBF)."""
import argparse
import csv
import json
import struct
import sys
import zlib
from pathlib import Path

class Ref:
 def __init__(self,n): self.id=n
 def __repr__(self):return f'Ref({self.id})'
class Nulls:
 def __init__(self,n):self.n=n

class Nrbf:
 def __init__(self, data):
  self.buf=memoryview(data);self.off=0;self.meta={};self.items={};self.libs={};self.root=None;self.counts={}
 def rd(self,n):
  if n<0 or self.off+n>len(self.buf):raise ValueError(f'out of bounds at {self.off} n={n}')
  v=self.buf[self.off:self.off+n].tobytes();self.off+=n;return v
 def u8(self):return self.rd(1)[0]
 def i32(self):return struct.unpack('<i',self.rd(4))[0]
 def u32(self):return struct.unpack('<I',self.rd(4))[0]
 def str(self):
  shift=0;n=0
  for i in range(5):
   v=self.u8();n|=(v&0x7f)<<shift
   if not (v&0x80):break
   shift+=7
  if n>1024*1024:raise ValueError('unexpected string length')
  return self.rd(n).decode('utf8','replace')
 def primitive(self,t):
  if t==1:return bool(self.u8())
  if t==2:return self.u8()
  if t==3:return self.rd(2).decode('utf-16le','replace')
  if t==5:return self.rd(16).hex()
  if t==6:return struct.unpack('<d',self.rd(8))[0]
  if t==7:return struct.unpack('<h',self.rd(2))[0]
  if t==8:return self.i32()
  if t==9:return struct.unpack('<q',self.rd(8))[0]
  if t==10:return struct.unpack('<b',self.rd(1))[0]
  if t==11:return struct.unpack('<f',self.rd(4))[0]
  if t in (12,13):return struct.unpack('<q',self.rd(8))[0]
  if t==14:return struct.unpack('<H',self.rd(2))[0]
  if t==15:return self.u32()
  if t==16:return struct.unpack('<Q',self.rd(8))[0]
  if t==17:return None
  if t==18:return self.str()
  raise ValueError(f'unsupported primitive {t} at {self.off}')
 def typeinfo(self,typ):
  if typ in (0,7):return self.u8()
  if typ==3:return self.str()
  if typ==4:return (self.str(),self.i32())
  if typ in (1,2,5,6):return None
  raise ValueError(f'bad binary type {typ} pos {self.off}')
 def memberval(self,typ,extra):
  if typ==0:return self.primitive(extra)
  while True:
   v=self.record()
   if v is not IGNORE:return v
 def classinfo(self):
  oid=self.i32(); name=self.str();n=self.i32()
  if n<0 or n>10000:raise ValueError('unexpected member count')
  names=[self.str() for i in range(n)]
  return oid,name,names
 def classvalues(self,oid,name,names,types,info):
  self.meta[oid]=(name,names,types,info)
  ret={'__class__':name,'__id__':oid}
  self.items[oid]=ret
  for n,t,e in zip(names,types,info):ret[n]=self.memberval(t,e)
  return ret
 def arr_values(self,n,t,e):
  if n<0 or n>100000:raise ValueError('unexpected arr len')
  out=[]
  while len(out)<n:
   v=self.memberval(t,e)
   if isinstance(v,Nulls):out.extend([None]*v.n)
   else:out.append(v)
  if len(out)!=n:raise ValueError(f'wrong array size {len(out)} vs {n}')
  return out
 def record(self):
  op=self.u8();self.counts[op]=self.counts.get(op,0)+1
  if op==0:
   self.root=self.i32();h=self.i32();major=self.i32();minor=self.i32()
   if major!=1:raise ValueError('unsupported version')
   return IGNORE
  if op==12:
   n=self.i32();self.libs[n]=self.str();return IGNORE
  if op==7:
   oid=self.i32();arrt=self.u8();rank=self.i32();lengths=[self.i32() for i in range(rank)]
   if arrt in (3,4,5):low=[self.i32() for i in range(rank)]
   bt=self.u8();extra=self.typeinfo(bt)
   out=[];self.items[oid]=out
   out.extend(self.arr_values(__import__('math').prod(lengths),bt,extra))
   return out
  if op in (4,5,2,3):
   oid,name,names=self.classinfo()
   if op in (4,5):
    types=[self.u8() for _ in names];info=[self.typeinfo(t) for t in types]
   else:types=[2]*len(names);info=[None]*len(names)
   if op in (3,5):lib=self.i32()
   return self.classvalues(oid,name,names,types,info)
  if op==1:
   oid=self.i32(); metadata_id=self.i32()
   name,names,types,info=self.meta[metadata_id]
   return self.classvalues(oid,name,names,types,info)
  if op==6:
   oid=self.i32();ret=self.str();self.items[oid]=ret;return ret
  if op==8:
   return self.primitive(self.u8())
  if op==9:return Ref(self.i32())
  if op==10:return None
  if op==13:return Nulls(self.u8())
  if op==14:return Nulls(self.i32())
  if op==15:
   oid=self.i32();n=self.i32();pt=self.u8();out=[self.primitive(pt) for i in range(n)];self.items[oid]=out;return out
  if op in (16,17):
   oid=self.i32();n=self.i32();out=[];self.items[oid]=out;out.extend(self.arr_values(n,2 if op==16 else 1,None));return out
  if op==11:return DONE
  raise ValueError(f'unknown record {op} at 0x{self.off-1:x}')
 def parse(self):
  while self.off<len(self.buf):
   r=self.record()
   if r is DONE:break
  return self.resolve(self.items[self.root])
 def resolve(self,x,stack=None):
  if isinstance(x,Ref):return self.resolve(self.items[x.id],stack)
  if isinstance(x,(dict,list)):
   if stack is None:stack=set()
   if id(x) in stack:return '<cycle>'
   stack.add(id(x))
   if isinstance(x,list):out=[self.resolve(v,stack) for v in x]
   else:out={k:self.resolve(v,stack) for k,v in x.items()}
   stack.remove(id(x));return out
  return x
IGNORE=object();DONE=object()
def decode(path):
 data=zlib.decompress(Path(path).read_bytes(),-15);parser=Nrbf(data);obj=parser.parse()
 return obj,parser
if __name__=='__main__':
 a=argparse.ArgumentParser();a.add_argument('ldd');a.add_argument('--json',help='write JSON output');a.add_argument('--csv',help='write CSV output');a.add_argument('--max',type=int,default=7);args=a.parse_args()
 obj,p=decode(args.ldd)
 print('NRBF read to offset',p.off,'of',len(p.buf),'objects',len(p.items),'record_counts',p.counts)
 print('root',type(obj).__name__,'items',len(obj) if hasattr(obj,'__len__') else '')
 for d in obj[:args.max]:print(json.dumps(d,ensure_ascii=False,indent=2))
 if args.json:Path(args.json).write_text(json.dumps(obj,indent=2,ensure_ascii=False),encoding='utf8')
 if args.csv:
  fields=[k for k in obj[0] if not k.startswith('__')]
  with Path(args.csv).open('w',newline='',encoding='utf-8-sig') as fo:
   w=csv.DictWriter(fo,fieldnames=fields,extrasaction='ignore');w.writeheader();w.writerows(obj)
