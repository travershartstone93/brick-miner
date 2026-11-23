"""Replay every frozen recipe against the unchanged first batch and check known alternative catalog compositions."""
from pathlib import Path
import os
import sys
from programs import programs,step
from transfer import requests,MIMES,build
import verify as v

def main():
    sys.path.insert(0,str(v.planner.SUITE))
    import check
    from fixtures import load_tasks
    tasks={t['id']:t for t in load_tasks()}
    v.planner.audit_requests()
    bundle={str(p.relative_to(v.BASE)):p.read_bytes() for p in v.BASE.rglob('*') if p.is_file()}
    os.environ['PKG_CONFIG_PATH']=str(v.BASE/'runtime/duckdb/lib/pkgconfig')
    all_arms={k:[ops] for stage in programs.values() for k,ops in stage.items()}
    assert all_arms.keys().isdisjoint(requests)
    all_arms.update(requests)
    assert all_arms.keys()==tasks.keys()
    fixtures=('ordinary','boundary','empty','transfer','sparse')
    def test(ident,arms,arm):
        task=tasks[ident]; stages=[]
        folder=Path.cwd()/arm
        for i,ops in enumerate(arms):
            dest=folder/'plans'/ident/str(i);dest.mkdir(parents=True)
            spec=v.spec(MIMES[task['domain']],ops,'application/json' if task['formats'][i]=='JSON' else 'text/plain')
            plan=v.pilot.plan(bundle,'',dest,spec=spec)
            stages.append(not plan['gaps'] and len(plan['providers'])==1 and plan['providers'][0]['status']=='complete')
        if not all(stages):return {'complete_roles':stages,'pass':False,'reason':'incomplete plan'}
        binary=build(bundle,task,arms,folder)
        checks=check.assess(task,binary,binary.parent/'fixtures',fixtures)['checks']
        return {'complete_roles':stages,'pass':all(x['pass_'] for x in checks.values()),'checks':checks}
    result={'fixed':{},'alternatives':{}}
    for k,arms in sorted(all_arms.items()):result['fixed'][k]=test(k,arms,'fixed')
    def xpath(query):return step('xml_text',xpath=query)
    reverse=step('reverse_order'); unique=step('unique_lines')
    alternatives={
        'H041':[[xpath("/feed/article[@state='live']/title"),unique]],
        'H043':[[xpath("/feed/article[@state='live'][title=../article[@state='draft']/title]/title"),unique,step('sort_lines')]],
        'H044':[[xpath('/feed/article/@section'),reverse,unique]],
        'H050':[[xpath("/feed/article[@state='live']/@editor"),unique,step('first_items',count=3)]],
        'H078':[[reverse,unique,reverse]]}
    for k,arms in alternatives.items():
        result['alternatives'][k]=dict(test(k,arms,'alternatives'),roles=arms)
    v.write_json('baseline.json',result)
    print('First batch:',sum(r['pass'] for r in result['fixed'].values()),'fixed recipes and',
          sum(r['pass'] for r in result['alternatives'].values()),'other catalog-only recipes passed.',flush=True)
if __name__=='__main__':main()
