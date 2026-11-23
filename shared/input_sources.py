"""Acquire two official C library ABIs and rebuild pugixml's C++ implementation from Arch debug sources."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import shlex
from mine import HERE,BASE,digest,run,ast_definition

def build(batch,contracts,provider_dir):
    work=Path(batch)
    work.mkdir()
    root=work/'source'
    root.mkdir()
    pins=json.loads((HERE/batch/'pins.json').read_bytes())
    cache=Path(os.environ.get('BRICK_INPUT_CACHE',pins['cache']))
    for name,value in pins['files'].items():
        if digest(cache/name)!=value: raise ValueError(f'input pin changed: {name}')
    ring=(root/'arch.gpg').resolve()
    run('gpg','--batch','--dearmor','--output',ring,'/usr/share/pacman/keyrings/archlinux.gpg')
    evidence={}
    archives={}
    for name in sorted(k for k in pins['files'] if k.endswith('.zst')):
        pkg=name.split('-')[0]
        status=subprocess.check_output(['gpgv','--keyring',ring,'--status-fd','1',str(cache/name)+'.sig',cache/name],text=True)
        signer,=re.findall(r'^\[GNUPG:\] VALIDSIG (\w+) ',status,re.M)
        recipe=pkg+'.PKGBUILD'
        info=subprocess.check_output(['bsdtar','-xOf',cache/name,'.BUILDINFO'],text=True)
        assert 'pkgbuild_sha256sum = '+pins['files'][recipe] in info.splitlines()
        archives[name]={'signer':signer,'sha256':pins['files'][name],'recipe_sha256':pins['files'][recipe]}
        evidence[recipe]=cache/recipe
        run('bsdtar','-xf',cache/name,'-C',root,'usr')
    for sub in ('usr/include','usr/share/licenses','usr/src/debug/pugixml'):
        for p in sorted((root/sub).rglob('*')):
            if p.is_file(): evidence[str(p.relative_to(root))]=p
    pugi,=root.rglob('pugixml.cpp')
    adapter=HERE/batch/'adapter.cpp'
    probe=HERE/batch/'probe.cpp'
    syms={s for v in contracts.values() for s in v['symbols']}
    paths={s:(probe if s.startswith('__wrap_') else pugi) for s in syms}
    assert 'xpath_query::evaluate_node_set(' in pugi.read_text()
    lib='brick_shared_inputs'
    flags=['-std=c++17','-O1','-g0','-fPIC','-DNDEBUG','-I'+str(root/'usr/include'),'-I'+str(provider_dir),'-I'+str(BASE/'headers'),*shlex.split(subprocess.check_output(['pkg-config','--cflags','glib-2.0'],text=True)),
           '-ffile-prefix-map='+str(Path.cwd())+'=/mined-build']
    declarations={'xpath_query::evaluate_node_set':ast_definition('xpath_query::evaluate_node_set',pugi,flags)}
    apis={'sqlite3_step':('sqlite3.h','libsqlite3.so.0'),'archive_read_next_header':('archive.h','libarchive.so.13'),
          'uriParseSingleUriA':('uriparser/Uri.h','liburiparser.so.1')}
    for symbol,(header,soname) in apis.items():
        ast=subprocess.check_output(['clang','-x','c','-I'+str(root/'usr/include'),'-fsyntax-only','-include',header,'-Xclang','-ast-dump=json','-Xclang','-ast-dump-filter='+symbol,'/dev/null'])
        text=ast.decode().lstrip()
        declarations_found=[]
        while text:
            item,end=json.JSONDecoder().raw_decode(text)
            if item.get("kind")=="FunctionDecl" and item.get("name")==symbol: declarations_found.append(item)
            text=text[end:].lstrip()
        decl,=declarations_found
        exported=subprocess.check_output(['nm','-D','--defined-only',root/'usr/lib'/soname],text=True)
        if decl.get('name')!=symbol or not re.search(r'\b'+symbol+r'(?:@@[^\s]+)?$',exported,re.M): raise RuntimeError('missing declared export '+symbol)
        declarations[symbol]={'header':header,'type':decl['type']['qualType'],'soname':soname}
    for mode in ('release','coverage'):
        dest=work/mode
        dest.mkdir()
        coverage=['-fprofile-instr-generate','-fcoverage-mapping'] if mode=='coverage' else []
        wraps=['-Wl,--wrap=sqlite3_step','-Wl,--wrap=archive_read_next_header','-Wl,--wrap=uriParseSingleUriA'] if mode=='coverage' else []
        units=[pugi,adapter]+([probe] if coverage else [])
        for i,src in enumerate(units):
            run('clang++',*flags,*coverage,'-c',src,'-o',dest/f'{i}.o')
        run('clang++','-shared','-Wl,-z,defs','-Wl,-Bsymbolic-functions','-Wl,-rpath,$ORIGIN','-Wl,-soname,lib'+lib+'.so',
            *coverage[:1],*wraps,*sorted(dest.glob('*.o')),'-L'+str(root/'usr/lib'),'-lsqlite3','-larchive','-luriparser','-lglib-2.0','-o',dest/('lib'+lib+'.so'))
    runtime=Path('runtime/lib')
    for soname in ('libsqlite3.so.0','libarchive.so.13','liburiparser.so.1'):
        shutil.copyfile(root/'usr/lib'/soname,runtime/soname)
    return lib,paths,{'package':'extra/pugixml 1.16-1, core/sqlite 3.53.3-1, core/libarchive 3.8.9-1, extra/uriparser 1.0.2-1',
        'source_id':'pugixml v1.16, sqlite-src-3530300, libarchive v3.8.9, uriparser 1.0.2','archives':archives,'definitions':declarations,
        'source_units':{s:str(f.relative_to(root)) if f.is_relative_to(root) else 'C API call probe: '+s.removeprefix('__wrap_') for s,f in paths.items()},
        'runtime_sha256':{n:digest(runtime/n) for n in ('libsqlite3.so.0','libarchive.so.13','liburiparser.so.1')},
        'headers':{n:digest(p) for n,p in evidence.items()},'adapter_sha256':digest(adapter),
        'kernel_sha256':digest(work/'release'/('lib'+lib+'.so'))},evidence
