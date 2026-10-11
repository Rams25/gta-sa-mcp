"""Offline allowlist generator: only fixed SA-MP UI symbols; never accepts RPC code addresses.
Requires pefile. Repeat --candidate DLL (adjacent .map). Run after each reviewed client build.
"""
import argparse,hashlib,json,re
from pathlib import Path
import pefile
import capstone
p=argparse.ArgumentParser();p.add_argument('--original',type=Path,required=True);p.add_argument('--candidate',type=Path,action='append',default=[]);p.add_argument('--score-sort-offset',type=lambda s:int(s,0),choices=[0x1c,0x40],help='Reviewed candidate constructor layout only; permits fallback when setter is linker-eliminated');a=p.parse_args()
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
 funcs.update({'selection_cancel':0x70fc0} if original else {'selection_cancel':sym('?EndSelect@CTextDrawPool@@'),'textdraw_pool':sym('?GetTextDrawPool@CNetGame@@',True),'netgame_ctor':sym('??0CNetGame@@'),'window_message':sym('?NewWndProc@@')})
 funcs.update({'score_update':0x6e760} if original else {'score_sort':sym('?SetSortMode@CScoreBoard@@',True),'score_update':sym('?UpdateList@CScoreBoard@@'),'score_ctor':sym('??0CScoreBoard@@')})
 funcs.update({'chat_submit':0x69410,'edit_text':0x85000,'list_key':0x8a500,'list_mouse':0x8a6e0,'dialog_key':0x60de0,'window_message':0x610d0} if original else {'chat_submit':sym('?ProcessInput@CCmdWindow@@'),'edit_text':sym('?SetText@CDXUTEditBox@@QAEXPBD_N@Z'),'list_key':sym('?HandleKeyboard@CDXUTListBox@@'),'list_mouse':sym('?HandleMouse@CDXUTListBox@@'),'dialog_message':sym('?MsgProc@CDialog@@')})
 funcs.update({'editor_mode':0x71c90,'editor_adjust':0x72f00,'editor_finish':0x72660,'editor_get_object':0x2e10,'entity_matrix':0x9e950} if original else {'editor_get':sym('?GetEditor@ObjectTools@@',True),'editor_mode':sym('?SetMode@CUnnamed2@@',True),'editor_adjust':sym('?Adjust@CUnnamed2@@',True),'editor_finish':sym('?Finish@CUnnamed2@@',True),'editor_get_object':sym('?GetAt@CObjectPool@@',True),'entity_matrix':sym('?GetMatrix@CEntity@@',True)})
 funcs.update({'chat_key':0x60de0,'editor_message':0x72ff0,'selection_process':0x70eb0,'selection_click':0x71010} if original else {'chat_key':sym('?HandleKeyPress@@'),'editor_message':sym('?MsgProc@CUnnamed2@@',True),'selection_process':sym('?ProcessSelection@CTextDrawPool@@'),'selection_click':sym('?OnClick@CTextDrawPool@@')})
 funcs.update({'object_selection_process':0x6d640,'object_selection_click':0x6d880} if original else {'object_selection_process':sym('?Process@CObjectSelection@@',True),'object_selection_click':sym('?MsgProc@CObjectSelection@@',True)})
 globals={'selection':0x2ac9f8,'editor':0x2ac9f0,'netgame':0x2aca24} if original else {'netgame':sym('?pNetGame@@')}
 globals.update({'death':0x2aca18} if original else {'death':sym('?pDeathWindow@@')})
 globals.update({} if original else {'selection_state':sym('?g_selection@@',True)})
 globals.update({'object_selection':0x2ac9f4} if original else {'object_selection_data':sym('?selection@',True)})
 globals.update({'score':0x2ac9dc,'chat':0x2aca14,'dialog':0x2ac9e0} if original else {'score':sym('?pScoreBoard@@'),'chat':sym('?pCmdWindow@@'),'dialog':sym('?pDialog@@')})
 telemetry=None
 if original or a.score_sort_offset==0x40:
  # Exact-byte witnesses for the two different native DXUT container ABIs.
  witness={'selected':0x888f0,'add':0x8a270,'scroll':0x88230} if original else {'selected':sym('?GetSelectedIndex@CDXUTListBox@@'),'add':sym('?AddItem@CDXUTListBox@@QAEJPBDHK@Z'),'scroll':sym('?Scroll@CDXUTScrollBar@@')}
  md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
  asm={k:'\n'.join(i.mnemonic+' '+i.op_str for i in md.disasm(pe.get_data(v,320 if k=='add' else 110),v)) for k,v in witness.items()}
  array,count,selected,scroll,position=(0x14c,0x150,0x143,0x5d,0x8e) if original else (0x158,0x15c,0x14c,0x80,0x90)
  for offset in (array,count,selected):assert hex(offset) in asm['selected'],(path,'selected',hex(offset))
  for offset in (0x29e,0x284,0x299,0x101,array,scroll):assert hex(offset) in asm['add'],(path,'add',hex(offset))
  assert hex(position) in asm['scroll'],(path,'scroll')
  globals.update({'chat_window':0x2aca10,'plate_renderer':0x2aca30} if original else {'chat_window':sym('?pChatWindow@@'),'plate_renderer':sym('?pLicensePlate@@')})
  telemetry={'array':array,'count':count,'selected':selected,'scroll':scroll,'position':position,'witness':witness}
 # Review14 read-only snapshot extension. Original SHA is fixed above; require
 # byte witnesses before permitting the common packed ABI, and map-pin owners.
 snapshot_witness={'manager_ctor':(0xd890,96),'manager_owner':(0x11dd3,78),
 'progress_update':(0x6a720,740),'progress_owner':(0xdc4c,16),
 'attached_set':(0xb079e,24),'attached_remove':(0xaea50,96),
 'remote_sync':(0x16da0,195),'remote_animation':(0x16452,100),
 'local_reset':(0x2f50,70),'jetpack':(0xac9e0,54),'player_get':(0x10f0,32),
 'bars':(0x6cd4f,72),'ped_action':(0xaba60,12)} if original else {}
 if original:
  review={k:'\n'.join(i.mnemonic+' '+i.op_str for i in md.disasm(pe.get_data(rva,n),rva)) for k,(rva,n) in snapshot_witness.items()}
  checks={'manager_ctor':['0x213','0x221','0x21f','0x220','0x21b'],
   'manager_owner':['0x102aca28'],'progress_owner':['0x102ac9e8'],
   'progress_update':['0x2c','0x24c','0x250','0x25c','[esi + 9]','[esi + 0xa]','[esi + 0xe]'],
   'attached_set':['0x34','0x74','0xd'],'attached_remove':['0x4c','0x27c'],
   'remote_sync':['0x1b0','0x1ac','0x18','0x1a'],
   'remote_animation':['0x1c0','0x1cd'],'local_reset':['0x100','0x104'],
   'jetpack':['0x2a4','0x46c','0x47c','0x10','0x8705c4'],
   'ped_action':['0x2a4','0x530'],'player_get':['0x26','[eax + 8]'],'bars':['0x1011c674','0x42c80000']}
  for k,needles in checks.items():
   for token in needles:assert token in review[k],(k,token)
  assert pe.get_data(0x11c674,4)==bytes.fromhex('4fecc43e')
  globals.update({'model_progress':0x2ac9e8,'artwork_manager':0x2aca28})
 else:
  globals.update({'model_progress':sym('?pModelProgress@@'),'artwork_manager':sym('?Manager@?')})
 # Existing older pins intentionally lack this opt-in and return null extensions.
 if telemetry is not None:telemetry.update({'snapshot_abi':1,'manager_indirect':original,
  'snapshot_witness':{k:{'rva':v[0],'size':v[1]} for k,v in snapshot_witness.items()}})
 reloc=[e.rva for b in getattr(pe,'DIRECTORY_ENTRY_BASERELOC',[]) for e in b.entries if e.type==3]
 checked={}
 for name,rva in funcs.items():
  if not rva:continue
  raw=pe.get_data(rva,24);mask=[1]*24
  for r in reloc:
   for i in range(max(0,r-rva),min(24,r-rva+4)):mask[i]=0
  checked[name]={'rva':rva,'bytes':list(raw),'mask':mask}
 builds.append({'sha256':sha,'timestamp':pe.FILE_HEADER.TimeDateStamp,'size':pe.OPTIONAL_HEADER.SizeOfImage,'original':original,'score_sort_offset':0x40 if original else a.score_sort_offset,'native_layout':original or a.score_sort_offset == 0x40,'globals':globals,'functions':checked,'telemetry':telemetry})
out=Path(__file__).resolve().parents[1]/'asi/src/commands/samp_ui_pins.hpp'
if out.exists():
 prior=json.loads(''.join(re.findall(r'R"pins\((.*?)\)pins"',out.read_text(),re.S)))
 current={b['sha256']:b for b in builds}
 builds=[current.pop(b['sha256'],b) for b in prior]+list(current.values())
payload=json.dumps(builds,separators=(',',':'))
literals='\n'.join('R"pins('+payload[i:i+8000]+')pins"' for i in range(0,len(payload),8000))
out.write_text('#pragma once\n// Generated from exact PE bytes; offline fixed-symbol allowlist.\ninline const char* SampUiPins =\n'+literals+';\n')
print(json.dumps([{'sha256':b['sha256'],'functions':list(b['functions'])} for b in builds],indent=2))
