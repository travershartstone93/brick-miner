"""Frozen interpretations for full user requests. Expected outputs come only from the independent suite."""
import json
from pathlib import Path
import sys

def step(outcome, **parameters):
    return outcome,parameters

def filter_(col, t, cmp, v):
    return step('filter_records', column=col,type=t,comparison=cmp,value=v)

def group(key, order='asc'):
    return step('group_rows',keys=key,group_order=order)

def select(which='first', order='', result='group'):
    return step('select_group_row',which=which,order_by=order,result_order=result)

def measure(op, field, order='group'):
    return step('aggregate_groups',measure=op,value_column=field,result_order=order)

def order(keys):
    return step('order_records',order_by=keys)

def report(cols):
    return step('report_fields',fields=cols)

def col(n):
    return step('ordered_column',column=n)

def rank(keys):
    return step('rank_records',keys=keys,order_by='encounter')

csv=step('read_csv_records',header='present')
def records(*fields): return step('read_json_records',fields=json.dumps(fields))
telemetry=records('sensor','room','minute','temp')
booking=records('id','room','start','stop','state')
active=filter_(4,'text','eq','active')
balance=step('running_total',keys='all',column=2)
duration=step('derive_integer',column=3,operation='subtract',operand='col:2')
programs={
 'readers':{
 'H031':[telemetry,step('threshold_entries',key_column=0,value_column=3,trigger='ge',threshold=8,reset='le',reset_threshold=4,emit='key')]},
 'relational':{
 'H001':[group('0'),select('last'),filter_(2,'int','lt','3'),col(0)],
 'H005':[group('0'),measure('distinct',1),filter_(1,'int','gt','1'),report('0,1')],
 'H017':[csv,step('derive_integer',column=2,operation='absolute',operand='self'),group('0'),select(order='3:num:desc'),report('0,1,2')],
 'H032':[telemetry,group('0'),select(order='3:num:desc'),report('0,2,3')],
 'H040':[telemetry,group('0'),select('last'),order('3:int:desc,0:text:asc'),col(0)],
 'H084':[group('1'),measure('distinct',0,'value_desc'),report('0,1')],
 'H090':[group('0,1'),select('last',result='source_asc'),report('0,1')],
 'H092':[booking,active,group('1'),select(order='2:num:asc,0:text:asc'),report('1,0')],
 'H093':[booking,active,filter_(2,'int','le','60'),filter_(3,'int','gt','60'),col(0),step('sort_lines')],
 'H095':[booking,active,duration,group('1'),select(order='5:num:asc,0:text:asc'),report('1,0,5')],
 'H099':[booking,filter_(4,'text','eq','canceled'),duration,group('1'),measure('sum',5,'value_desc'),report('0,1')]},
 'history':{
 'H013':[csv,filter_(0,'text','eq','bank'),rank('1,2'),filter_(3,'int','gt','1'),report('pos')],
 'H015':[csv,filter_(0,'text','eq','book'),balance,report('pos,3')],
 'H018':[csv,filter_(0,'text','eq','bank'),balance,filter_(3,'int','lt','0'),report('pos,3'),step('first_items',count=1)],
 'H083':[rank('0,1'),filter_(2,'int','gt','1'),report('pos')]},
 'relations':{
 'H085':[step('reachable_nodes',from_column=0,to_column=1,seed='core')],
 'H086':[step('reachable_nodes',from_column=1,to_column=0,seed='app')]}
}

scans=step('sqlite_records',table='scans',fields='parcel,depot,state,minute,seq',order_by='seq:asc')
articles=step('xml_records',records='/feed/article',fields='@editor,@section,@state,title')
live=filter_(2,'text','eq','live')
def zip_fields(cols): return step('zip_file_records',fields=cols)
programs['inputs']={
 'H021':[scans,group('0'),select('last'),filter_(2,'text','eq','out'),col(0)],
 'H024':[scans,group('1'),measure('distinct',0,'value_desc'),report('0,1')],
 'H029':[scans,group('1'),select('last',result='source_desc'),col(1),step('first_items',count=3)],
 'H041':[articles,live,col(3),step('unique_lines')],
 'H045':[articles,live,group('1'),select('last'),report('1,3')],
 'H046':[articles,rank('0,3'),filter_(4,'int','gt','1'),report('0,3')],
 'H049':[articles,live,group('3'),measure('distinct',0),filter_(1,'int','ge','2'),report('0,1')],
 'H050':[articles,live,col(0),step('unique_lines'),step('first_items',count=3)],
 'H052':[zip_fields('base,parent'),group('0'),measure('distinct',1),filter_(1,'int','gt','1'),report('0,1')],
 'H053':[zip_fields('path,size'),order('1:int:desc,0:text:asc'),report('0,1'),step('first_items',count=3)],
 'H054':[zip_fields('top,size'),group('0'),measure('sum',1,'value_desc'),report('0,1')],
 'H055':[zip_fields('path,size'),filter_(1,'int','eq','0'),col(0),step('sort_lines')],
 'H059':[zip_fields('ext,size'),filter_(0,'text','ne',''),group('0'),measure('sum',1,'value_desc'),report('0,1')],
 'H060':[zip_fields('parent,path,size'),filter_(2,'int','gt','0'),group('0'),select(order='2:num:asc,1:text:asc'),report('0,1,2')]}

def annotate(keys,col,op,value=''):return step('annotate_group_measure',keys=keys,column=col,measure=op,value=value)
def intervals(relation):return step('match_intervals',key=1,id=0,start=2,stop=3,relation=relation)
programs['analytics']={
 'H030':[scans,annotate('1',2,'count_equal','delivered'),annotate('1',2,'count_equal','out'),filter_(5,'int','eq','0'),filter_(6,'int','gt','0'),group('1'),select(),report('1,6')],
 'H038':[telemetry,annotate('0',3,'lower_median'),group('0'),select(),report('0,4')],
 'H039':[telemetry,annotate('0,2',3,'min'),annotate('0,2',3,'max'),filter_(4,'int_field','ne','5'),group('0,2'),select(),order('0:text:asc,2:int:asc'),report('0,2,4,5')],
 'H042':[articles,annotate('0',2,'count_equal','draft'),annotate('0',2,'count_equal','live'),group('0'),select(),report('0,4,5')],
 'H091':[booking,active,intervals('overlap'),report('0,1')],
 'H097':[booking,active,intervals('touches'),report('0,1')],
 'H098':[booking,active,intervals('contains'),col(0),step('unique_lines')]}

def tree(cols):return step('tree_file_records',fields=cols)
def uri(cols):return step('uri_records',fields=cols)
def lag(keys,n):return step('previous_field',keys=keys,column=n)
def calc(n,op,rhs):return step('derive_integer',column=n,operation=op,operand=rhs)
programs['inputs'].update({
 'H061':[tree('base,parent'),group('0'),measure('distinct',1),filter_(1,'int','gt','1'),col(0)],
 'H062':[tree('path,depth,size'),filter_(1,'int','gt','0'),filter_(2,'int','eq','0'),col(0)],
 'H063':[tree('top'),filter_(0,'text','ne','.'),group('0'),measure('count',0),report('0,1')],
 'H066':[tree('top,path,size'),filter_(0,'text','ne','.'),group('0'),select(order='2:num:desc,1:text:asc'),report('0,1')],
 'H068':[tree('folded_base,base'),group('0'),measure('distinct',1),filter_(1,'int','gt','1'),report('0,1')],
 'H072':[uri('original,scheme'),filter_(1,'text','eq','https'),col(0),step('unique_lines'),step('first_items',count=3)],
 'H076':[uri('base,original'),group('0'),measure('distinct',1),filter_(1,'int','ge','2'),col(0)],
 'H078':[uri('original'),group('0'),select('last',result='source_asc'),col(0)],
 'H079':[uri('scheme'),group('0','encounter'),measure('count',0),report('0,1')],
 'H080':[uri('original,scheme,has_fragment'),filter_(1,'text','eq','https'),filter_(2,'int','eq','1'),col(0),step('unique_lines'),step('sort_lines')]})
programs['history'].update({
 'H003':[lag('0',2),filter_(3,'text','eq','1'),calc(4,'subtract','col:2'),filter_(5,'int','gt','0'),group('0'),select(order='5:num:desc'),order('5:int:desc,0:text:asc'),report('0,5')],
 'H007':[lag('0',1),lag('0',2),filter_(3,'int','eq','1'),filter_(1,'text_field','eq','4'),filter_(2,'int_field','eq','6'),report('0,pos')],
 'H033':[telemetry,lag('0',2),filter_(4,'text','eq','1'),calc(2,'subtract','col:5'),filter_(6,'int','gt','10'),report('0,5,2')],
 'H036':[telemetry,lag('0',1),filter_(4,'text','eq','1'),filter_(1,'text_field','ne','5'),report('0,5,1')]})
programs['inputs'].update({
 'H025':[scans,lag('0',1),filter_(5,'text','eq','1'),filter_(1,'text_field','ne','6'),report('0,1')],
 'H028':[scans,lag('0',3),filter_(5,'text','eq','1'),calc(3,'subtract','col:6'),filter_(7,'int','ge','30'),order('7:int:desc,4:int:asc'),report('0,7')]})
programs['analytics'].update({
 'H035':[telemetry,calc(2,'divide','int:1440'),annotate('1,4',3,'min'),annotate('1,4',3,'max'),calc(6,'subtract','col:5'),group('1,4'),select(),order('1:text:asc,4:int:asc'),report('1,4,7')],
 'H069':[tree('path,depth'),annotate('all',1,'max'),filter_(1,'int_field','eq','2'),col(0)]})

def member(n,pool,values,mode='keep'):return step('filter_membership',column=n,pool=pool,values=values,mode=mode)
def carry(keys,order,frame,condition,value,n):return step('first_matching_field',keys=keys,order_by=order,frame=frame,condition=condition,value=value,field=n)
ends=[booking,active,step('interval_endpoints',key=1,start=2,stop=3),step('running_total',keys='0',column=2)]
programs['sequences']={
 'H009':[carry('0','encounter','before','2:int:eq','0',2),filter_(3,'int','eq','1'),filter_(2,'int','gt','0'),group('0'),select(result='source_asc'),report('0,pos')],
 'H023':[scans,carry('0','encounter','before','2:text:eq','pickup',3),filter_(5,'text','eq','1'),filter_(2,'text','eq','delivered'),group('0'),select(),calc(3,'subtract','col:6'),report('0,7')],
 'H026':[scans,carry('0','encounter','before','2:text:eq','delivered',2),filter_(5,'text','eq','1'),filter_(2,'text','ne','delivered'),group('0'),select(result='source_asc'),col(0)],
 'H056':[zip_fields('path,payload'),carry('1','0:text:asc','whole','always','',0),filter_(0,'text_field','ne','3'),order('0:text:asc'),report('0,3')],
 'H065':[tree('path,ext,component=build'),filter_(2,'text','eq','0'),member(1,'list','["cpp","hpp"]'),col(0)],
 'H073':[uri('host,original,scheme'),member(2,'list','["http","https"]'),group('0'),measure('distinct',1,'value_desc'),report('0,1')],
 'H081':[member(0,'column','1','exclude'),col(0),step('unique_lines'),step('sort_lines')],
 'H082':[member(1,'column','0','exclude'),col(1),step('unique_lines'),step('sort_lines')],
 'H094':[*ends,lag('0',1),lag('0',3),filter_(6,'int','eq','1'),filter_(7,'int','gt','0'),calc(1,'subtract','col:5'),group('0'),measure('sum',8),report('0,1')],
 'H100':[*ends,group('0'),select(order='3:num:desc'),report('0,3')]}

def choose(c,t,cmp,val,yes,no):return step('conditional_field',column=c,type=t,comparison=cmp,value=val,when_true=yes,when_false=no)
def witness(n):return step('graph_witnesses',source=0,target=1,length=n)
programs['decisions']={
 'H011':[csv,calc(2,'add','int:0'),annotate('1,3',0,'count_equal','book'),annotate('1,3',0,'count_equal','bank'),rank('0,1,3'),choose(0,'text','eq','book','field:5','field:4'),filter_(6,'int_field','gt','7'),report('pos')],
 'H012':[csv,choose(0,'text','eq','book','text:1','text:-1'),calc(2,'divide','col:3'),group('1'),measure('sum',4),filter_(1,'int','ne','0'),report('0,1')],
 'H034':[telemetry,choose(3,'int','le','5','text:1','text:0'),step('running_total',keys='0',column=4),filter_(3,'int','gt','5'),rank('0,5'),filter_(6,'int','eq','3'),report('0,2')],
 'H048':[articles,step('field_text_length',column=3),group('1'),select(order='4:num:desc'),report('1,3')],
 'H087':[witness('closure'),filter_(0,'text_field','eq','2'),col(0),step('unique_lines')],
 'H089':[witness('2'),filter_(3,'text','eq','1'),filter_(0,'text_field','ne','1'),filter_(1,'text_field','ne','2'),filter_(0,'text_field','ne','2'),order('0:text:asc,2:text:asc'),report('0,2'),step('unique_lines')]}

def evaluate(v,base,stage,batch):
    sys.path.insert(0,str(v.planner.SUITE))
    import check
    from fixtures import load_tasks
    tasks={t['id']:t for t in load_tasks()}
    mime={0:'text/tab-separated-values',1:'text/csv',2:'application/vnd.sqlite3',3:'application/json',4:'application/xml',
          5:'application/zip',6:'inode/directory',7:'text/uri-list',8:'text/tab-separated-values',9:'application/json'}
    receipts={}
    for ident,ops in programs.get(batch,{}).items():
        task=tasks[ident]
        folder=v.WORK/batch/'requests'/ident
        output='text/plain' if task['formats']==['text'] else 'application/json'
        spec=v.spec(mime[task['domain']],ops,output)
        new=[s[0] for s in ops if s[0] in v.load(v.HERE/batch/'contracts.json')]
        v.program(base,folder/'before',spec,[])
        built=v.program(stage,folder/'after',spec,new)
        result=check.assess(task,built,folder/'evaluation',('ordinary','boundary','empty','transfer','sparse'))
        if not all(check['pass_'] for check in result['checks'].values()):
            raise RuntimeError(f'{ident} failed: {result}')
        v.program(v.pilot.ablate(stage,new[-1]),folder/'removed',spec,[])
        receipts[ident]={'checks':result['checks'],'operations':ops,'baseline':'incomplete','removed':new[-1]}
    return receipts
