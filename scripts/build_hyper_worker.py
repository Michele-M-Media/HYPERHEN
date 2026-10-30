#!/usr/bin/env python3
"""Rebuild the R45 Hyper worker against checksum-pinned release AI libraries.

This optional developer recipe does not download models or change the installed SDK.
The ordinary release build embeds the retained, verified worker instead.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, json, os, subprocess

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path)
    parser.add_argument('--jobs',type=int,default=4)
    parser.add_argument('--check-only',action='store_true')
    args=parser.parse_args()
    root=Path(__file__).resolve().parent.parent
    sdk=Path(os.environ['PS5_PAYLOAD_SDK']).resolve()
    build=(args.build_dir or root/'build-hyper-worker').resolve()
    if args.jobs<1:parser.error('--jobs must be positive')
    recipe=json.loads((root/'source/hyper/build-recipe.json').read_text())
    def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    for name,expected in recipe['libraries_sha256'].items():
        p=root/'release-inputs/hyper-ai/lib'/name
        if digest(p)!=expected:raise ValueError(f'AI library checksum mismatch: {name}')
    crt=root/'release-inputs/sdk/crt1-r42-1360.o'
    if digest(crt)!=recipe['crt_sha256']:raise ValueError('Isolated CRT checksum mismatch')
    def expand(token):
        return token.replace('@ROOT@',str(root)).replace('@SDK@',str(sdk)).replace('@BUILD@',str(build))
    commands=[[expand(t) for t in command] for command in recipe['commands']]
    link=[expand(t) for t in recipe['link']]
    if link[-1]!=str(crt) or '-nostartfiles' not in link:raise ValueError('Unexpected CRT linkage')
    for command in commands:
        if not Path(command[command.index('-c')+1]).is_file():raise ValueError('Missing worker source')
        if not Path(command[0]).is_file():raise ValueError('Missing SDK compiler')
    if args.check_only:
        print('R45 worker inputs and isolated CRT: PASS');return
    build.mkdir(parents=True,exist_ok=True)
    for command in commands:(build/command[command.index('-o')+1]).parent.mkdir(parents=True,exist_ok=True)
    def compile_one(command):subprocess.run(command,cwd=build,check=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:list(pool.map(compile_one,commands))
    subprocess.run(link,cwd=build,check=True)
    output=Path(link[link.index('-o')+1])
    print(f'Built {output}\nSHA256 {digest(output)}')
    print('Use a separate test build before replacing the retained worker. Console behavior is not validated by compilation.')

if __name__=='__main__':main()
