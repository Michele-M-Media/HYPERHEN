from pathlib import Path
import hashlib
import json
import struct
import subprocess

import argparse
parser=argparse.ArgumentParser(description='Create isolated R42-compatible CRT; original SDK remains unchanged')
parser.add_argument('--original',type=Path,required=True)
parser.add_argument('--output-dir',type=Path,required=True)
args=parser.parse_args()
HERE=args.output_dir.resolve()
NATIVE=HERE
ORIGINAL=args.original.resolve()
EXPECTED = '0e4c1e5e38f22026580e856238a53e75d9e112f99a34bfe5495a3a0ffac15238'

def sha(data):
    return hashlib.sha256(data).hexdigest()

class Elf:
    def __init__(self, raw):
        self.raw = raw
        assert raw[:6] == b'\x7fELF\x02\x01'
        self.shoff = struct.unpack_from('<Q', raw, 40)[0]
        self.entsize, self.n, self.stridx = struct.unpack_from('<HHH', raw, 58)
        self.headers = [list(struct.unpack_from('<IIQQQQIIQQ', raw, self.shoff+i*self.entsize)) for i in range(self.n)]
        h = self.headers[self.stridx]
        strings = raw[h[4]:h[4]+h[5]]
        self.sections = {}
        for i, h in enumerate(self.headers):
            name = strings[h[0]:strings.index(b'\0', h[0])].decode()
            self.sections[name] = (i, h)
    def data(self, name):
        _, h = self.sections[name]
        return self.raw[h[4]:h[4]+h[5]] if h[1] != 8 else b''
    def symbol(self, name):
        _, h = self.sections['.symtab']
        linked = self.headers[h[6]]
        strings = self.raw[linked[4]:linked[4]+linked[5]]
        for at in range(h[4], h[4]+h[5], h[9]):
            n, info, other, section, value, size = struct.unpack_from('<IBBHQQ', self.raw, at)
            if strings[n:strings.index(b'\0',n)].decode() == name:
                return at, info, other, section, value, size
        raise KeyError(name)

raw = ORIGINAL.read_bytes()
assert sha(raw) == EXPECTED
old = Elf(raw)
name = '.text.__kernel_init'
code = old.data(name)
assert len(code) == 0xbf0
assert code[0x127:0x134] == bytes.fromhex('4181fe000040130f85820a0000')
assert code[0xbb6:0xbbf] == bytes.fromhex('4883c4205b415e5dc3')
assert old.symbol('__kernel_init')[-1] == len(code)
relocs = old.data('.rela.text.__kernel_init')
records = [struct.unpack_from('<QQq', relocs, i) for i in range(0,len(relocs),24)]
by_offset = {r[0]: r for r in records}

# Preserve the complete installed CRT, including all existing dispatcher paths.
# Redirect only its unsupported-13.xx edge to an appended exact-13.60 block.
# The new constants are byte-verified against the working public 0.9.2 ELF
# in the accompanying pre-existing SDK diagnosis; no offsets are inferred.
handler = bytearray(bytes.fromhex('4181fe000060130f85'))
handler += struct.pack('<i', 0xbb6-(len(code)+13))
body_at = len(code)+len(handler)
body = bytearray(code[0x134:0x171])
assert body[6:10] == struct.pack('<i', -0xcb0000)
assert body[0x28:0x2c] == struct.pack('<i', 0xd99064)
assert body[0x39:0x3d] == struct.pack('<i', 0x3137510)
body[6:10] = struct.pack('<i', -0xcc0000)
body[0x28:0x2c] = struct.pack('<i', 0xd9c064)
body[0x39:0x3d] = struct.pack('<i', 0x314b510)
handler += body
root_store_at = len(code)+len(handler)
handler += code[0xa7e:0xa88]
handler += b'\x48\x05'+struct.pack('<I',0x2098228)
handler += b'\xe9'+struct.pack('<i',0xa8e-(len(code)+len(handler)+5))
patched_code = bytearray(code)
patched_code[0x130:0x134] = struct.pack('<i',len(code)-0x134)
patched_code += handler
extra = []
for source in (0x141,0x152,0x163):
    r = by_offset[source]
    extra.append((body_at+source-0x134,r[1],r[2]))
r = by_offset[0xa81]
extra.append((root_store_at+3,r[1],r[2]))
patched_relocs = relocs + b''.join(struct.pack('<QQq', *r) for r in extra)
NATIVE.mkdir(parents=True,exist_ok=True)
(NATIVE/'kernel-init.bin').write_bytes(patched_code)
(NATIVE/'kernel-init.rela').write_bytes(patched_relocs)
candidate = NATIVE/'crt1.o'
assert not candidate.exists(), 'Refusing to overwrite existing isolated CRT'
contents = {n: old.data(n) for n in old.sections}
contents[name] = bytes(patched_code)
contents['.rela.text.__kernel_init'] = patched_relocs
symtab = bytearray(contents['.symtab'])
symbol_at,_,_,_,_,_ = old.symbol('__kernel_init')
symbol_relative = symbol_at-old.sections['.symtab'][1][4]
struct.pack_into('<Q',symtab,symbol_relative+16,len(patched_code))
contents['.symtab'] = bytes(symtab)
# LLVM objcopy deliberately refuses updates of relocation sections. Repack
# this ET_REL object directly, preserving section identities and symbol indexes.
# The only semantic changes are the two named sections and function symbol size.
assert struct.unpack_from('<Q',raw,32)[0] == 0
result = bytearray(raw[:64])
headers = [h.copy() for h in old.headers]
names_by_index = {i:n for n,(i,_) in old.sections.items()}
for i,h in enumerate(headers):
    if i == 0:
        assert not any(h)
        continue
    n = names_by_index[i]
    align = h[8] or 1
    result.extend(b'\0'*((-len(result))%align))
    h[4] = len(result)
    if h[1] != 8:
        h[5] = len(contents[n])
        result.extend(contents[n])
result.extend(b'\0'*((-len(result))%8))
section_table_at = len(result)
for h in headers:
    result.extend(struct.pack('<IIQQQQIIQQ',*h))
struct.pack_into('<Q',result,40,section_table_at)
candidate.write_bytes(result)
new = Elf(result)
assert set(old.sections) == set(new.sections)
changed = [n for n in old.sections if old.data(n) != new.data(n)]
assert sorted(changed) == sorted([name,'.rela.text.__kernel_init','.symtab']),changed
assert new.symbol('__kernel_init')[-1] == len(patched_code)
for n in old.sections:
    if n in changed:
        continue
    assert old.data(n) == new.data(n),n
assert new.data(name)[:0x130] == code[:0x130]
assert new.data(name)[0x134:len(code)] == code[0x134:]
assert new.data('.rela.text.__kernel_init')[:len(relocs)] == relocs
assert ORIGINAL.read_bytes() == raw
report = {
    'result':'CRT_PREPARED; tests pending',
    'parent_crt':str(ORIGINAL),'parent_sha256':sha(raw),
    'isolated_crt':str(candidate),'sha256':sha(result),
    'changed_section_payloads':changed,
    'unchanged_section_payloads':len(old.sections)-len(changed),
    'changes':'One unsupported-13.xx branch redirected to appended exact 13.60 profile; four existing-symbol relocations cloned; __kernel_init symbol size extended.',
    'no_installed_sdk_mutation':True,
    'original_dispatcher_bytes_changed':'Only 4-byte relative jump displacement at 0x130; all other original function bytes unchanged',
    'new_case':'13.60 only; 13.42 remains unsupported in this narrowly scoped change',
    'new_constants':{'TEXT_BASE': '-0xcc0000','ALLPROC':'0x28c9e80','SECURITY_FLAGS':'0xd9c064','ROOTVNODE':'0x314b510','BUS_DATA_DEVICES':'0x2098228'},
    'common_suffix':'Original 0xa8e onward, including VMSPACE root 0x1d0, derived addresses, native error resolver and original cleanup paths',
    'new_text_bytes':len(patched_code), 'original_text_bytes':len(code),
    'extra_relocation_offsets':[hex(r[0]) for r in extra],
    'command':['python3',str(Path(__file__).resolve())],
    'rationale':'Use exact installed runtime without newer vendored kernel/IOMMU changes; add only the missing supported firmware branch.'
}
(HERE/'crt-build-verification.json').write_text(json.dumps(report,indent=2)+'\n')
(HERE/'kernel-init.bin').write_bytes(patched_code)
print(json.dumps({k:report[k] for k in ('result','isolated_crt','sha256','changed_section_payloads','unchanged_section_payloads')}))
