#!/usr/bin/env python3
"""Read actual ARM ELF init entries; reject central-before-BLE/settings ordering."""
import argparse,json,re,struct
from pathlib import Path

def verify(elf,mapfile):
    raw=Path(elf).read_bytes()
    if raw[:6]!=b'\x7fELF\x01\x01':raise ValueError('Expected little-endian ELF32')
    h=struct.unpack_from('<16sHHIIIIIHHHHHH',raw)
    sections=[struct.unpack_from('<10I',raw,h[6]+i*h[11]) for i in range(h[12])]
    def memory(address,size):
        for s in sections:
            if s[1]!=8 and s[3]<=address and address+size<=s[3]+s[5]:
                return raw[s[4]+address-s[3]:s[4]+address-s[3]+size]
        raise ValueError('Unmapped ELF address '+hex(address))
    symbols=[]
    for s in sections:
        if s[1]!=2:continue
        strings=sections[s[6]];table=raw[strings[4]:strings[4]+strings[5]]
        for at in range(s[4],s[4]+s[5],s[9]):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',raw,at)
            end=table.find(b'\0',name)
            symbols.append((table[name:end].decode(),value,size,info&15))
    def unique_symbol(name):
        found=[s for s in symbols if s[0]==name]
        if len(found)!=1:raise ValueError('Expected unique ELF symbol '+name)
        return found[0][1]&~1
    text=Path(mapfile).read_text()
    def central_function(owner):
        pattern=r' \.text\.zmk_split_bt_central_init\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+([^\n]+)'
        found=[int(m[1],16)&~1 for m in re.finditer(pattern,text) if owner in m[2] and int(m[1],16)]
        if len(found)!=1:raise ValueError('Cannot identify central init owner '+owner)
        return found[0]
    def entry_for(function):
        found=[]
        for name,address,size,kind in symbols:
            if name.startswith('__init_') and size==8:
                callback,device=struct.unpack('<II',memory(address,8))
                if (callback&~1)==function:found.append(address)
        if len(found)!=1:raise ValueError('Expected one SYS_INIT entry for '+hex(function))
        return found[0]
    addresses=dict(ble=entry_for(unique_symbol('zmk_ble_init')),
                   central=entry_for(central_function('app/libapp.a(central.c.obj)')),
                   observer=entry_for(central_function('central_status_changed_observer.c.obj')))
    if not addresses['ble']<addresses['central']<addresses['observer']:
        raise ValueError('Unsafe BLE/settings -> split central -> observer init order: '+str(addresses))
    return {k:hex(v) for k,v in addresses.items()}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',required=True);p.add_argument('--map',required=True);a=p.parse_args()
    try:result=verify(a.elf,a.map)
    except (ValueError,OSError) as error:p.exit(1,str(error)+'\n')
    print('PASS actual ELF startup order BLE/settings -> split central -> observer:',json.dumps(result))
