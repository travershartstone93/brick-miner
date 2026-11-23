"""Qualify each brick from independent files, then replay fixed native planner and ablation arms."""
import collections
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
from time import monotonic

import mine
import programs
import input_sources
import sqlite3
import zipfile
from mine import HERE, BASE, BATCHES, digest, run, write_json
sys.path.insert(0,str(HERE.parent/"row-ops"))
import qualify as planner
pilot = planner.pilot
WORK = Path.cwd()
RUNTIME = WORK/"runtime"
ABI = BASE/"providers"

def load(path):
    return json.loads(path.read_bytes())

def encode_string(value):
    data = value.encode()
    return 'std::string("' + ''.join('\\%03o'%n for n in data) + '",'+str(len(data))+')'

def native_flags(package):
    return shlex.split(subprocess.check_output(["pkg-config","--cflags","--libs",package],text=True))

def generate_consumer(c, contract, client):
    is_reader = contract["from"].startswith("in:")
    is_rows = contract["to"]=="rows"
    params = [encode_string(x) if isinstance(x,str) else str(x) for x in c["args"]]
    first = "file" if is_reader else "brick_data::tab_records(file)"
    call = "brick_miner::shared::"+c["brick"]+"("+", ".join([first,*params])+")"
    # Compare positions along with all fields. For a string port compare the complete sequence.
    header = '#include "miner_'+c["brick"]+'.hpp"\n#include "tab_provider.hpp"\n#include <cstdio>\n'
    if is_rows:
        positions = c.get("positions",list(range(1,len(c["rows"])+1)))
        expected = ",".join("{"+str(pos)+",{"+",".join(map(encode_string,row))+"}}"
                            for pos,row in zip(positions,c["rows"],strict=True))
        header += "using Output=std::vector<std::pair<std::int64_t,brick_data::Fields>>;\n"
        body = f'Output out; {call}([&](std::int64_t i,const brick_data::Fields& f){{out.emplace_back(i,f);}});return out;'
    else:
        expected = ",".join(map(encode_string,c["strings"]))
        header += "using Output=std::vector<std::string>;\n"
        body = "return "+call+";"
    (client/"case.hpp").write_text(header+"const Output expected={"+expected+"};\nOutput collect(const char* file){"+body+"}\n")

def spec(mime, ops, output="application/json"):
    return {"input_type":mime,"roles":[{"id":"r","content":"whole","multiple":False,"output_type":output,
            "requirement":{"operations":[{"outcome":name,"parameters":args} for name,args in ops],"uncovered":[]}}]}

def program(stage, folder, task, required):
    folder.mkdir(parents=True)
    result = pilot.plan(stage,"",folder,spec=task)
    if not required:
        planner.reject_complete(result)
        return None
    p, = result["providers"]
    assert not result["gaps"] and p["status"]=="complete" and all(pilot.card_id(k) in p["bricks"] for k in required), result
    (folder/"component.hpp").write_text(p["source"])
    (folder/"main.cpp").write_text(planner.MAIN)
    packages = sorted({pkg for h in load(folder/"components/helpers.json") for pkg in h["packages"]})
    run("g++-15","-std=c++17","-O0","-I",folder/"bricks",folder/"main.cpp","-o",folder/"program","-Wl,-rpath,/usr/local/lib",*pilot.flags(packages))
    return folder/"program"

def materialize(path,content):
    if isinstance(content,str):
        path.write_text(content)
    elif "tree" in content:
        path.mkdir()
        for name,data in content["tree"]:
            f=path/name
            f.parent.mkdir(exist_ok=True,parents=True)
            f.write_bytes(bytes.fromhex(data))
        for name,dest in content.get("symlinks",{}).items(): (path/name).symlink_to(dest)
    elif "sqlite" in content:
        with sqlite3.connect(path) as db:
            db.executescript(content["sqlite"])
    else:
        with zipfile.ZipFile(path,"w",zipfile.ZIP_DEFLATED) as archive:
            for name,hexdata in content["zip"]:
                archive.writestr(name,bytes.fromhex(hexdata))

def check_case(base,stage,batch,c,contract,pkg):
    folder = WORK/batch/"checks"/c["id"]
    folder.mkdir(parents=True)
    generate_consumer(c,contract,folder)
    binary = folder/"consumer"
    run("clang++","-std=c++17","-O0","-I",WORK/batch/"headers","-I",ABI,"-I",BASE/"headers",
        "-I",folder,HERE/"tests/consumer.cpp","-o",binary,*native_flags(pkg))
    src = folder/"input"
    materialize(src,c["input"])
    run(binary,src,"pass")
    env = {**os.environ,"LD_LIBRARY_PATH":str(WORK/batch/"coverage")+":"+str(RUNTIME/"lib"),
           "LLVM_PROFILE_FILE":str(folder/"native.profraw")}
    run(binary,src,"pass",env=env)
    broken = folder/"invalid"
    materialize(broken,c["invalid"])
    run(binary,broken,"fail")
    run(binary,folder/"nonexistent","fail")
    name = c["brick"]
    operations = [(name,dict(zip(contract["args"],c["args"],strict=True)))]
    if contract["to"]=="rows":
        operations.append(("ordered_column",{"column":c["column"]}))
    task = spec(contract["from"][3:] if contract["from"].startswith("in:") else "text/tab-separated-values",operations)
    program(base,folder/"before",task,[])
    generated = program(stage,folder/"after",task,[name])
    program(pilot.ablate(stage,name),folder/"removed",task,[])
    for f,content,ok in [("ordinary",c["input"],True),("empty",c["empty"],True),
                         ("invalid",c["invalid"],False),("missing",None,False)]:
        path,out = folder/(f+".input"),folder/(f+".output")
        if content is not None: materialize(path,content)
        status = subprocess.run([generated,path,out],timeout=10).returncode
        if ok:
            wanted = ([r[c["column"]] for r in c["rows"]] if contract["to"]=="rows" else c["strings"]) if f=="ordinary" else []
            assert status==0 and load(out)==wanted, (c["id"],f,status,wanted)
        else:
            assert status!=0 and not out.exists(),(c["id"],f,status)
    return {"input_family":c["id"],"native":"all fields and positions matched","planner":"passed",
            "baseline":"no complete plan","provider_removed":"no complete plan","checks":["ordinary","empty","invalid","missing"]}

def coverage(batch,paths,c,lib,execution=None):
    folder = WORK/batch/"checks"/c["id"]
    merged = folder/"native.profdata"
    run("llvm-profdata","merge","-sparse",folder/"native.profraw","-o",merged)
    export = json.loads(subprocess.check_output(["llvm-cov","export",lib,"-instr-profile="+str(merged),
                                                 *[str(p) for p in sorted(set(paths.values()))]]))["data"][0]
    hit={}
    for sym,source in paths.items():
        active = [f for f in export["functions"] if f["count"] and any(fn.endswith(str(source)) for fn in f["filenames"])]
        # When a definition in a TU has multiple specializations use the explicit symbol token.
        target=(execution or {}).get(sym,sym)
        names = [f["name"] for f in active if target.split("::")[-1] in f["name"] and
                 (len(target.split("::"))==1 or target.split("::")[0] in f["name"])]
        if not names:
            raise RuntimeError(f'{c["id"]}: no execution in extracted {sym}; active entries: {[f["name"] for f in active][:16]}')
        hit[sym] = sorted(set(names))
    return hit

def qualify(base,batch):
    contracts = load(HERE/batch/"contracts.json")
    cases = load(HERE/batch/"cases.json")
    assert all(sum(c["brick"]==k for c in cases)>=2 for k in contracts)
    started=monotonic()
    name,paths,metadata,originals = (input_sources.build if batch=="inputs" else mine.build)(batch,contracts,ABI)
    built_at=monotonic()
    lib = "lib"+name+".so"
    shutil.copyfile(WORK/batch/"release"/lib,RUNTIME/"lib"/lib)
    pc = RUNTIME/"lib/pkgconfig"/(name+".pc")
    pc.write_text('prefix=${pcfiledir}/../..\nlibdir=${prefix}/lib\nName: '+name+
                  '\nDescription: Extracted '+batch+' modules\nVersion: 1.5.6\nLibs: -L${libdir} -Wl,-rpath,${libdir} -l'+name+' -lduckdb\n')
    headers = {k:mine.declarations(k,v).encode() for k,v in contracts.items()}
    header_dir = WORK/batch/"headers"
    header_dir.mkdir()
    for k,data in headers.items(): (header_dir/(pilot.card_id(k)+".hpp")).write_bytes(data)
    meta = {k:{"package":metadata["package"],"upstream_revision":metadata["source_id"],
               "units":{s:metadata["source_units"][s] for s in v["symbols"]},"native_sha256":metadata["kernel_sha256"],
               "provenance":"qualifications/shared/"+batch+"/source.json","binding":"project-authored typed adapter"}
            for k,v in contracts.items()}
    tasks = {k:{**c,"call":"brick_miner::shared::"+k,"packages":[name],"origin":"mined_application"} for k,c in contracts.items()}
    if batch=="inputs":
        inputs={"sqlite_records":("core/sqlite 3.53.3-1","sqlite-src-3530300","sqlite3_step"),
                "xml_records":("extra/pugixml 1.16-1","v1.16","xpath_query::evaluate_node_set"),
                "zip_file_records":("core/libarchive 3.8.9-1","v3.8.9","archive_read_next_header"),
                "tree_file_records":("core/libarchive 3.8.9-1","v3.8.9","archive_read_next_header"),
                "uri_records":("extra/uriparser 1.0.2-1","1.0.2","uriParseSingleUriA")}
        for k,(package,revision,symbol) in inputs.items():
            meta[k].update(package=package,upstream_revision=revision,selected_symbol=symbol)
            if k!="xml_records": tasks[k]["origin"]="mined_api"
    stage = pilot.arm_bundle(base,headers,meta,tasks=tasks)
    receipts={}
    for c in cases:
        k = c["brick"]
        result=check_case(base,stage,batch,c,contracts[k],name)
        result["executed"] = coverage(batch,{s:paths[s] for s in contracts[k]["symbols"]},c,WORK/batch/"coverage"/lib,contracts[k].get("execution"))
        receipts[c["id"]] = result
    origin = "qualifications/shared/"+batch+"/"
    for f,p in originals.items(): stage["qualifications/shared/upstream/"+("formats/" if batch=="inputs" else "")+f] = p.read_bytes()
    for f in ("contracts.json","cases.json","adapter.cpp"):
        stage[origin+f] = (HERE/batch/f).read_bytes()
    if batch not in ("readers","inputs"): stage[origin+"table.hpp"]=(HERE/"table.hpp").read_bytes()
    if batch=="inputs":
        stage[origin+"probe.cpp"]=(HERE/batch/"probe.cpp").read_bytes()
        for soname in metadata["runtime_sha256"]:
            stage["runtime/duckdb/lib/"+soname]=(RUNTIME/"lib"/soname).read_bytes()
        stage[origin+"package.json"]=(HERE/batch/"pins.json").read_bytes()
    else:
        stage[origin+"package.json"]=(HERE.parent/"row-ops/mined/source/arch.json").read_bytes()
    checked_at=monotonic()
    stage[origin+"requests.json"]=pilot.encode(programs.evaluate(sys.modules[__name__],base,stage,batch))
    stage[origin+"source.json"]=pilot.encode(metadata)
    stage[origin+"receipts.json"]=pilot.encode(receipts)
    stage["runtime/duckdb/lib/"+lib]=(RUNTIME/"lib"/lib).read_bytes()
    stage["runtime/duckdb/lib/pkgconfig/"+pc.name]=pc.read_bytes()
    stage.pop("SHA256SUMS",None)
    checksum = pilot.finish_manifest(stage)
    draft=WORK/batch/"staging"
    draft.mkdir()
    pilot.write_bundle(stage,draft)
    run("sha256sum","--check","--strict","--quiet","SHA256SUMS",cwd=draft)
    draft.rename(WORK/batch/"bundle")
    write_json(WORK/batch/"report.json",{"batch":batch,"cards":len(json.loads(stage["catalog.json"])),
        "new_cards":list(contracts),"manifest_sha256":checksum,"cases":receipts})
    write_json(WORK/batch/"timing.json",{"acquire_and_build_seconds":round(built_at-started,2),
        "brick_checks_seconds":round(checked_at-built_at,2),"end_user_checks_and_staging_seconds":round(monotonic()-checked_at,2)})
    print(f"{batch}: {len(contracts)} qualified, {checksum}",flush=True)
    return stage

def main(last,cache,base_path=BASE,first=0):
    run("mountpoint","-q","/run/media/whisper/UNLLM_CORPUS")
    assert digest(BASE/"SHA256SUMS")==mine.BASE_HASH
    run("sha256sum","--check","--strict","--quiet","SHA256SUMS",cwd=BASE)
    if base_path!=BASE: run("sha256sum","--check","--strict","--quiet","SHA256SUMS",cwd=base_path)
    shutil.copytree(base_path/"runtime/duckdb",RUNTIME,dirs_exist_ok=True)
    os.environ["PKG_CONFIG_PATH"] = str(RUNTIME/"lib/pkgconfig")
    base = {str(p.relative_to(base_path)):p.read_bytes() for p in base_path.rglob("*") if p.is_file()}
    mine.acquire(cache)
    for batch in BATCHES[first:BATCHES.index(last)+1]:
        base=qualify(base,batch)
if __name__=="__main__":
    main(sys.argv[1],Path(sys.argv[2]),*( [Path(sys.argv[3]),int(sys.argv[4])] if len(sys.argv)>3 else [] ))
