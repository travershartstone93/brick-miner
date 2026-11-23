"""Compose remaining supported cases from the saved bundle, using the suite's own five-fixture evaluator."""
from pathlib import Path
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from programs import programs

import verify as v
from programs import step,group,select,measure,order,report,col,rank,lag,annotate,calc,tree,uri,filter_,csv,scans,articles,booking,telemetry,live,zip_fields

def ranking(keys,by):return step('rank_records',keys=keys,order_by=by)
def counts(keys,n,*values):return [annotate(keys,n,'count_equal',value) for value in values]
def positive(n):return filter_(n,'int','gt','0')
def unique():return step('unique_lines')
def alpha():return step('sort_lines')
def side(name):return filter_(0,'text','eq',name)
stock=[group('0'),select('last')]
parcel=[scans,group('0'),select('last')]
article_states=[articles,*counts('1',2,'draft','live')]
room_states=[booking,*counts('1',4,'active','canceled')]
requests={
'H002':[[step('threshold_entries',key_column=0,value_column=2,trigger='le',threshold=2,reset='ge',reset_threshold=5,emit='key')]],
'H004':[[annotate('1',2,'min'),group('1,3'),measure('distinct',0),report('0,2,1')]],
'H006':[[*stock,filter_(2,'int','eq','0'),col(0)],[*stock,positive(2),filter_(2,'int','le','2'),col(0)]],
'H008':[[group('1,2'),select(),ranking('1','2:int:asc'),filter_(3,'int','le','2'),order('1:text:asc,2:int:asc'),report('1,2')]],
'H010':[[col(1),step('reverse_order'),unique()]],
'H014':[[csv,group('1,0'),select(),ranking('1','0:text:asc'),ranking('1','0:text:desc'),filter_(3,'int','eq','1'),filter_(4,'int','eq','1'),order('1:text:asc'),report('1,0')]],
'H016':[[csv,side('book'),filter_(2,'int','lt','0'),col(1),unique(),alpha()],[csv,side('bank'),group('1'),measure('sum',2),filter_(1,'int','lt','0'),col(0)]],
'H019':[[csv,step('running_total',keys='1,0',column=2),rank('1,0'),group('1,0'),select('last'),lag('1',3),lag('1',4),positive(5),filter_(3,'int_field','eq','6'),filter_(4,'int_field','ne','8'),col(1)]],
'H020':[[csv,*counts('1,2',0,'book','bank'),positive(3),positive(4),group('1,2'),select(),order('1:text:asc,2:int:asc'),report('1,2')]],
'H022':[[scans,lag('0',2),filter_(2,'text','eq','delivered'),filter_(6,'text','ne','delivered'),report('0,4')]],
'H027':[[*parcel,filter_(2,'text','eq','delivered'),report('0,1')],[*parcel,filter_(2,'text','ne','delivered'),report('0,1')]],
'H037':[[telemetry,filter_(3,'int','lt','0'),col(0),unique(),alpha()],[telemetry,annotate('0',3,'max'),filter_(4,'int','le','0'),col(0),unique(),alpha()]],
'H043':[[articles,group('3'),measure('distinct',2),filter_(1,'int','eq','2'),col(0)]],
'H044':[[articles,group('1'),select('last',result='source_desc'),col(1)]],
'H047':[[*article_states,positive(4),positive(5),col(1),unique(),alpha()],[*article_states,positive(4),filter_(5,'int','eq','0'),col(1),unique(),alpha()]],
'H051':[[step('archive_files'),step('basenames'),unique()]],
'H057':[[zip_fields('path,depth'),filter_(1,'int','eq','0'),col(0)],[zip_fields('path,depth'),positive(1),col(0)]],
'H058':[[zip_fields('ext,parent'),filter_(0,'text','eq','csv'),col(1),unique(),alpha()]],
'H064':[[tree('path,payload'),ranking('1','0:text:desc'),group('1'),select(order='0:text:asc'),filter_(2,'int','ge','2'),order('0:text:asc'),report('0,2')]],
'H067':[[tree('path,hidden_name,hidden_parent'),positive(1),col(0)],[tree('path,hidden_name,hidden_parent'),positive(2),col(0)]],
'H070':[[tree('parent,ext'),*counts('0',1,'','txt'),positive(2),positive(3),col(0),unique(),alpha()]],
'H071':[[step('reverse_order')]],
'H074':[[step('adjacent_unique')]],
'H075':[[uri('original,scheme'),filter_(1,'text','eq','file'),col(0)],[uri('original,scheme'),filter_(1,'text','eq','https'),col(0)]],
'H077':[[uri('original'),rank('0'),ranking('0','pos:int:desc'),filter_(1,'int','eq','2'),calc(2,'add','int:1'),report('0,3')]],
'H088':[[filter_(0,'text_field','eq','1'),col(0),unique(),alpha()],
        [step('graph_witnesses',source=0,target=1,length='2'),filter_(0,'text_field','eq','2'),filter_(0,'text_field','lt','1'),report('0,1'),unique()]],
'H096':[[*room_states,filter_(5,'int','eq','0'),positive(6),col(1),unique(),alpha()],[*room_states,positive(5),positive(6),col(1),unique(),alpha()]]
}
MIMES=['text/tab-separated-values','text/csv','application/vnd.sqlite3','application/json','application/xml','application/zip','inode/directory','text/uri-list','text/tab-separated-values','application/json']
def build(bundle,task,arms,root=None):
    work=(root or Path.cwd())/task['id']
    work.mkdir()
    sources=[];packages=set()
    for i,ops in enumerate(arms):
        dest=work/str(i);dest.mkdir()
        spec=v.spec(MIMES[task['domain']],ops,'application/json' if task['formats'][i]=='JSON' else 'text/plain')
        plan=v.pilot.plan(bundle,'',dest,spec=spec)
        p,=plan['providers']
        if plan['gaps'] or p['status']!='complete':raise RuntimeError(f'incomplete role {i}: {plan["gaps"]}')
        (dest/'component.hpp').write_text(p['source'])
        source=dest/'arm.cpp'
        source.write_text('#define component_0 arm_'+str(i)+'\n#include "component.hpp"\n')
        sources.append(source)
        packages.update(q for h in v.load(dest/'components/helpers.json') for q in h['packages'])
    # Each arm gets its own translation unit and its own copied provider headers.
    objects=[]
    for i,src in enumerate(sources):
        obj=src.with_suffix('.o');objects.append(obj)
        # An out-of-line wrapper also works for the emitter's static/inline definitions.
        with src.open('a') as f:f.write(f'void run_{i}(const char* in,const char* out){{arm_{i}(in,out);}}\n')
        v.run('g++-15','-std=c++17','-I',src.parent/'bricks',*v.pilot.flags(sorted(packages)),'-c',src,'-o',obj)
    main=work/'main.cpp'
    main.write_text(''.join(f'void run_{i}(const char*,const char*);\n' for i in range(len(arms)))+
        'int main(int n,char**v){try{if(n!='+str(2+len(arms))+')return 2;'+''.join(f'run_{i}(v[1],v[{i+2}]);' for i in range(len(arms)))+'}catch(...){return 1;}}\n')
    binary=work/'program'
    v.run('g++-15','-std=c++17',main,*objects,'-Wl,-rpath,/usr/local/lib',*v.pilot.flags(sorted(packages)),'-o',binary)
    return binary
def main(root):
    sys.path.insert(0,str(v.planner.SUITE))
    from fixtures import load_tasks
    import check
    os.environ['PKG_CONFIG_PATH']=str(root/'runtime/duckdb/lib/pkgconfig')
    bundle={str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*') if p.is_file()}
    tasks={t['id']:t for t in load_tasks()}
    results={}
    effects=set(json.loads(bundle['providers/contracts.json'])['outcomes'])
    final=(root/'qualifications/shared'/v.BATCHES[-1]/'receipts.json').is_file()
    frozen={k:[ops] for stage in programs.values() for k,ops in stage.items()}
    if not frozen.keys().isdisjoint(requests):raise RuntimeError('duplicate frozen request IDs')
    frozen.update(requests)
    if set(frozen)!=set(tasks):raise RuntimeError('incomplete request inventory')
    def evaluate(ident):
        arms=frozen[ident]
        task=tasks[ident]
        missing={name for arm in arms for name,_ in arm}-effects
        if missing and not final:
            return {'passed':False,'skipped':'not available in this earlier bundle','missing':sorted(missing)}
        try:
            binary=build(bundle,task,arms)
            result=check.assess(task,binary,binary.parent/'fixtures',('ordinary','boundary','empty','transfer','sparse'))
            return {'roles':arms,'checks':result['checks'],'passed':all(x['pass_'] for x in result['checks'].values())}
        except (ValueError,KeyError,RuntimeError,subprocess.CalledProcessError) as error:
            return {'passed':False,'error':str(error)}
    with ThreadPoolExecutor(max_workers=2) as pool:
        for ident,result in zip(sorted(frozen),pool.map(evaluate,sorted(frozen)),strict=True):
            results[ident]=result
            print(ident,result['passed'],flush=True)
            v.write_json('transfer.json',results)
    failures=[k for k,r in results.items() if not r['passed'] and 'skipped' not in r]
    if failures:raise RuntimeError('Failed original requests: '+', '.join(failures))
if __name__=='__main__':main(Path(sys.argv[1]))
