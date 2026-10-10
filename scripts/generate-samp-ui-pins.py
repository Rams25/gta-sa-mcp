"""Offline allowlist generator: only fixed SA-MP UI symbols; never accepts RPC code addresses.
Requires pefile. Repeat --candidate DLL (adjacent .map). Run after each reviewed client build.
"""
import argparse,hashlib,json,re
from pathlib import Path
import pefile
p=argparse.ArgumentParser();p.add_argument('--original',type=Path,required=True);p.add_argument('--candidate',type=Path,action='append',default=[]);a=p.parse_args()
builds=[]
for path in [a.original]+a.candidate:
 pe=pefile.PE(str(path));sha=hashlib.sha256(path.read_bytes()).hexdigest();original=path==a.original
 if original:assert sha=='bccdb297464bd382625635be25585df07a8fa6668bc0015650708e3eb4ffcd4b'
 assert pe.FILE_HEADER.Machine==0x14c and pe.OPTIONAL_HEADER.Magic==0x10b
 symbols={}
 if not original:
  for line in path.with_suffix('.map').read_text().splitlines():
   m=re.match(r'\s*[0-9a-fA-F]+:[0-9a-fA-F]+\s+(\S+)\s+([0-9a-fA-F]{8})\s',line)
   if m:symbols[m[1]]=int(m[2],16)-pe.OPTIONAL_HEADER.ImageBase
 def sym(prefix,optional=False):
  found=[v for k,v in symbols.items() if k.startswith(prefix)]
  if optional and not found:return 0
  assert len(found)==1,(prefix,found)
  return found[0]
 funcs={'score_open':0x6ee10,'score_close':0x6e410,'chat_open':0x68ec0,'chat_close':0x68fc0,'help_open':0x6b570,'help_close':0x6f2a0} if original else {
 'score_open':sym('?Show@CScoreBoard@@'),'score_close':sym('?Hide@CScoreBoard@@'),'chat_open':sym('?Enable@CCmdWindow@@'),'chat_close':sym('?Disable@CCmdWindow@@'),'help_open':sym('?ShowHelpDialog@@',True),'help_close':sym('?Hide@CDialog@@')}
 globals={'score':0x2ac9dc,'chat':0x2aca14,'dialog':0x2ac9e0} if original else {'score':sym('?pScoreBoard@@'),'chat':sym('?pCmdWindow@@'),'dialog':sym('?pDialog@@')}
 reloc=[e.rva for b in getattr(pe,'DIRECTORY_ENTRY_BASERELOC',[]) for e in b.entries if e.type==3]
 checked={}
 for name,rva in funcs.items():
  if not rva:continue
  raw=pe.get_data(rva,24);mask=[1]*24
  for r in reloc:
   for i in range(max(0,r-rva),min(24,r-rva+4)):mask[i]=0
  checked[name]={'rva':rva,'bytes':list(raw),'mask':mask}
 builds.append({'sha256':sha,'timestamp':pe.FILE_HEADER.TimeDateStamp,'size':pe.OPTIONAL_HEADER.SizeOfImage,'original':original,'globals':globals,'functions':checked})
out=Path(__file__).resolve().parents[1]/'asi/src/commands/samp_ui_pins.hpp'
out.write_text('#pragma once\n// Generated from exact PE bytes; offline fixed-symbol allowlist.\ninline const char* SampUiPins = R"pins('+json.dumps(builds,separators=(',',':'))+')pins";\n')
print(json.dumps([{'sha256':b['sha256'],'functions':list(b['functions'])} for b in builds],indent=2))
