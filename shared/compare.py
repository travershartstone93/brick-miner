"""Compare extraction to linking each same project-authored adapter straight to the packaged API."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from time import monotonic
import mine
import verify as v

def owner(pair,batch):
    folder=pair/'run-1'/batch
    if (folder/'report.json').exists():return pair/'run-1'
    parent=json.loads((pair/'predecessor.json').read_bytes())
    return owner(Path(parent['pair']),batch)

def main(pair,bundle):
    out={}
    for batch in mine.BATCHES:
        if not (bundle/'qualifications/shared'/batch).is_dir():continue
        t=monotonic()
        run=owner(pair,batch)
        frozen=run.parent/'miner-inputs'
        dest=Path.cwd()/'comparison'/batch;dest.mkdir(parents=True)
        flags=['-std=c++17','-O1','-fPIC','-DNDEBUG','-shared','-Wl,-z,defs','-Wl,-rpath,$ORIGIN','-I'+str(frozen),
               '-I'+str(bundle/'providers'),'-I'+str(bundle/'headers')]
        if batch=='inputs':
            sdk=run/batch/'source/usr'
            flags+=['-I'+str(sdk/'include'),*shlex.split(subprocess.check_output(['pkg-config','--cflags','glib-2.0'],text=True))]
            libs=['-L'+str(sdk/'lib'),'-lpugixml','-larchive','-lsqlite3','-luriparser','-lglib-2.0']
            shutil.copyfile(sdk/'lib/libpugixml.so.1',dest/'libpugixml.so.1')
        else:
            sdk=run/'sources/package/usr'
            original=run/'sources/debug/usr/src/debug/duckdb/duckdb'
            flags+=['-I'+str(sdk/'include'),'-I'+str(original/'third_party/yyjson/include')]
            libs=['-L'+str(sdk/'lib'),'-lduckdb']
        lib='libbrick_shared_'+batch+'.so'
        mine.run('clang++',*flags,'-Wl,-soname,'+lib,frozen/batch/'adapter.cpp',*libs,'-o',dest/lib)
        elapsed=monotonic()-t
        env={**os.environ,'LD_LIBRARY_PATH':str(dest)+':'+str(bundle/'runtime/duckdb/lib')}
        cases=json.loads((frozen/batch/'cases.json').read_bytes())
        for c in cases:
            f=run/batch/'checks'/c['id']
            for name,behavior in [('input','pass'),('invalid','fail'),('nonexistent','fail')]:
                mine.run(f/'consumer',f/name,behavior,env=env)
        extra_cases=[c for c in v.load(mine.HERE/'tests/additional-cases.json') if c['batch']==batch]
        for c in extra_cases:
            folder=dest/c['id'];folder.mkdir()
            contract=v.load(frozen/batch/'contracts.json')[c['brick']]
            v.generate_consumer(c,contract,folder)
            binary=folder/'consumer'
            mine.run('clang++','-std=c++17','-I',bundle/'headers','-I',bundle/'providers','-I',folder,
                     mine.HERE/'tests/consumer.cpp','-o',binary,'-L'+str(bundle/'runtime/duckdb/lib'),
                     '-lbrick_shared_'+batch,'-lduckdb')
            for label,key,behavior in [('input','input','pass'),('invalid','invalid','fail'),('missing',None,'fail')]:
                path=folder/label
                if key:v.materialize(path,c[key])
                for library in (dest,run/batch/'coverage',bundle/'runtime/duckdb/lib'):
                    mine.run(binary,path,behavior,env={**env,'LD_LIBRARY_PATH':str(library)+':'+str(bundle/'runtime/duckdb/lib'),
                              'LLVM_PROFILE_FILE':str(folder/(label+'.profraw'))})
            # The ordinary instrumented run must enter the selected upstream window implementation.
            mine.run('llvm-profdata','merge','-sparse',folder/'input.profraw','-o',folder/'input.profdata')
            listing=subprocess.check_output(['llvm-cov','export',run/batch/'coverage'/lib,'-instr-profile='+str(folder/'input.profdata')])
            hits=[f['name'] for f in json.loads(listing)['data'][0]['functions'] if f['count'] and
                  'WindowAggregateExecutor' in f['name'] and 'EvaluateInternal' in f['name']]
            if not hits:raise RuntimeError('supplemental submeter case did not reach the extracted window function')
            mine.write_json(folder/'evidence.json',{'case':c,'executed':hits})
        distinct_inputs={k:{json.dumps(c['input'],sort_keys=True) for c in [*cases,*extra_cases] if c['brick']==k}
                         for k in v.load(frozen/batch/'contracts.json')}
        if any(len(values)<2 for values in distinct_inputs.values()):raise RuntimeError('a brick lacks distinct inputs')
        out[batch]={'cases':[c['id'] for c in cases],'additional_cases':[c['id'] for c in extra_cases],'checks':3*(len(cases)+len(extra_cases)),'binding_build_seconds':round(elapsed,2),
                    'kernel_route':json.loads((run/batch/'timing.json').read_bytes()),
                    'direct_binding_sha256':mine.digest(dest/lib)}
        print('Compared',batch,len(cases),flush=True)
    mine.write_json('comparison.json',out)
if __name__=='__main__': main(Path(sys.argv[1]),Path(sys.argv[2]))
