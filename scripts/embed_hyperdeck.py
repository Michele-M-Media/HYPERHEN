#!/usr/bin/env python3
"""Generate build-only C assets for websrv and the dedicated Media launcher."""
import argparse
import mimetypes
import struct
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--assets', required=True)
p.add_argument('--logo', required=True)
p.add_argument('--output', required=True)
p.add_argument('--launcher-only', action='store_true')
a = p.parse_args()
root = Path(a.assets)
parts = ['#include <stddef.h>\n#include <string.h>\nvoid asset_register(const char*,void*,unsigned long,const char*);\n']
launcher_icon = root/'icon0.png' if a.launcher_only else Path(a.logo)
if a.launcher_only:
    png = launcher_icon.read_bytes()
    valid = (len(png) >= 33 and png[:8] == b'\x89PNG\r\n\x1a\n' and
             png[12:16] == b'IHDR' and
             struct.unpack('>II', png[16:24]) == (512, 512) and
             png[24] == 8 and png[25] == 2 and png[28] == 0)
    if not valid:
        raise SystemExit('HyperDeck launcher icon must be a non-interlaced 512x512 8-bit RGB PNG')
items = [('hyperdeck_icon', launcher_icon, '/logo.png', 'image/png'),
         ('hyperdeck_param', root/'param.json', None, None)]
if not a.launcher_only:
    textual = {'.html': 'text/html; charset=utf-8',
               '.css': 'text/css; charset=utf-8',
               '.js': 'application/javascript; charset=utf-8',
               '.json': 'application/json; charset=utf-8',
               '.svg': 'image/svg+xml'}
    asset_index = 0
    for path in sorted(p for p in root.rglob('*') if p.is_file() and p.name != 'param.json'):
        relative = path.relative_to(root)
        if any(part.startswith('.') for part in relative.parts):
            continue
        mime = textual.get(path.suffix.lower()) or mimetypes.guess_type(path.name)[0] or 'application/octet-stream'
        items.append((f'hyperdeck_asset_{asset_index}', path, '/' + relative.as_posix(), mime))
        asset_index += 1
for symbol, path, url, mime in items:
    data = path.read_bytes()
    values = ','.join(str(b) for b in data)
    parts.append(f'const unsigned char {symbol}[]={{'+values+'};\n')
    parts.append(f'const unsigned long {symbol}_size=sizeof({symbol});\n')
if not a.launcher_only:
    parts.append('const unsigned char *hyperdeck_selector_template(const char *url, unsigned long *size) {\n')
    # One presentation template is rendered for both legacy PS5 HEN and 5.xx
    # Bypervisor. Keep its existing URLs for the working 5.xx WebView path.
    selector_templates = {'/selector-5xx-r10-20261025.html',
                          '/selector-5xx-r10-20261025.js'}
    for symbol, _, url, _ in items:
        if url in selector_templates:
            parts.append(f'if (!strcmp(url,"{url}")) {{ *size=sizeof({symbol}); return {symbol}; }}\n')
    parts.append('return NULL;\n}\n')
    parts.append('__attribute__((constructor)) static void register_hyperdeck_assets(void) {\n')
    for symbol, _, url, mime in items:
        if url:
            parts.append(f'asset_register("{url}",(void*){symbol},sizeof({symbol}),"{mime}");\n')
    parts.append('}\n')
Path(a.output).write_text(''.join(parts))
