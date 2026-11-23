"""Locate source implementations in the signed DuckDB tree and rebuild them with the row ABI."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess

HERE = Path(__file__).resolve().parent
BASE = Path("/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/mined-rows-3ej65t4_/run-1/bundle")
BASE_HASH = "79dcd99668d0cec01d192d81e01ae1c7bb18222a4581013c937c5eea4ea07a91"
CACHE = Path("/run/media/whisper/UNLLM_CORPUS/unllm/experiments/brick-miner/duckdb-input-GHUYik9e")
TREE = Path("sources/debug/usr/src/debug/duckdb/duckdb")
SDK = Path("sources/package/usr")
BATCHES = ["readers", "relational", "history", "relations", "inputs", "analytics", "sequences", "decisions"]

def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)

def digest(path):
    with open(path,"rb") as stream:
        return hashlib.file_digest(stream,"sha256").hexdigest()

def write_json(path, obj):
    Path(path).write_text(json.dumps(obj,sort_keys=True,indent=2,ensure_ascii=False)+"\n")

def acquire(cache):
    import sys
    sys.path.insert(0,str(HERE.parent/"row-ops/mined/source"))
    import importlib.util
    source = HERE.parent/"row-ops/mined/source/mine.py"
    spec = importlib.util.spec_from_file_location("signed_duckdb",source)
    original = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(original)
    cwd = Path.cwd()
    (cwd/"sources/original").mkdir(parents=True)
    try:
        os.chdir(cwd/"sources")
        original.acquire(cache)
    finally:
        os.chdir(cwd)

def declarations(name, contract):
    src = "const char*" if contract["from"].startswith("in:") else "const brick_data::Rows&"
    out = {"rows":"brick_data::Rows","strings":"std::vector<std::string>"}[contract["to"]]
    types = {"text":"const std::string&","column":"std::size_t","integer":"std::int64_t"}
    args = ", ".join([src]+[types[v] for v in contract["args"].values()])
    return f'#pragma once\n#include "provider_types.hpp"\nnamespace brick_miner::shared {{\n{out} {name}({args});\n}}\n'

def locate(contracts):
    wanted = sorted({s for c in contracts.values() for s in c["symbols"]})
    expressions = {s:re.compile(r"\b"+re.escape(s)+r"\s*\([^;{}]*\)\s*(?:const\s*)?\{",re.S) for s in wanted}
    found = {s:[] for s in wanted}
    for p in sorted(TREE.rglob("*.cpp")):
        data = p.read_text(errors="replace")
        for sym in wanted:
            if sym in data and expressions[sym].search(data):
                found[sym].append(p)
    for sym, files in found.items():
        if len(files) != 1:
            raise RuntimeError(f"{sym}: expected one definition, found {files}")
    return {s:found[s][0] for s in wanted}

def ast_definition(symbol,source,flags):
    args=["clang++",*flags,"-fsyntax-only","-Xclang","-ast-dump=json","-Xclang","-ast-dump-filter="+symbol,str(source)]
    raw=subprocess.check_output(args,text=True).lstrip()
    candidates=[]
    while raw:
        node,end=json.JSONDecoder().raw_decode(raw)
        raw=raw[end:].lstrip()
        if node.get("name")==symbol.split("::")[-1] and any(n["kind"]=="CompoundStmt" for n in node.get("inner",[])):
            name=node.get("mangledName",symbol)
            qualified=subprocess.check_output(["c++filt",name],text=True).strip()
            if re.search(r"(?<![A-Za-z_0-9])"+re.escape(symbol)+r"(?:\(|$)",qualified):
                candidates.append({"name":name,"type":node["type"]["qualType"]})
    if len(candidates)!=1: raise RuntimeError(f"{source}: expected one compiled definition of {symbol}, found {candidates}")
    return candidates[0]

def build(batch, contracts, provider_dir):
    work = Path(batch)
    work.mkdir(exist_ok=True)
    paths = locate(contracts)
    includes = [SDK/"include",TREE/"third_party/utf8proc/include",TREE/"third_party/yyjson/include",
                TREE/"third_party/fast_float",TREE/"third_party/fmt/include",TREE/"extension/core_functions/include",provider_dir,HERE]
    flags = ["-std=c++17","-O1","-g0","-DNDEBUG","-fPIC","-ffile-prefix-map="+str(Path.cwd())+"=/mined-build"]
    inc = ["-I"+str(d) for d in includes]
    adapter = HERE/batch/"adapter.cpp"
    name = "brick_shared_"+batch
    units = sorted(set(paths.values()))+[adapter]
    metadata = {"source_units":{sym:str(p.relative_to(TREE)) for sym,p in paths.items()},
                "source_id":"069cc9f9b5","package":"extra/duckdb 1.5.6-1","headers":{},
                "compile_flags":flags[:-1],"compiler":subprocess.check_output(["clang++","--version"],text=True).splitlines()[0],
                "adapter_sha256":digest(adapter)}
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        metadata["definitions"]=dict(zip(paths,pool.map(lambda sym:ast_definition(sym,paths[sym],[*flags,*inc]),paths),strict=True))
    for mode in ("release","coverage"):
        dest = work/mode
        dest.mkdir(exist_ok=True)
        probe = ["-fprofile-instr-generate","-fcoverage-mapping"] if mode=="coverage" else []
        def compile_one(pair):
            index, src = pair
            obj = dest/f"unit{index}.o"
            run("clang++",*flags,*probe,*inc,"-MMD","-MF",obj.with_suffix(".d"),"-c",src,"-o",obj)
            return obj
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            objs = list(pool.map(compile_one,enumerate(units)))
        run("clang++","-shared","-Wl,-z,defs","-Wl,-Bsymbolic-functions","-Wl,-rpath,$ORIGIN",
            "-Wl,-soname,lib"+name+".so",*probe[:1],*objs,"-L"+str(SDK/"lib"),"-lduckdb","-o",dest/("lib"+name+".so"))
    originals = {}
    for dep in sorted((work/"release").glob("*.d")):
        for raw in shlex.split(dep.read_text().replace("\\\n"," ").split(":",1)[1]):
            p = Path(raw)
            if p.is_relative_to(TREE):
                rel = str(p.relative_to(TREE))
            elif p.is_relative_to(SDK/"include"):
                rel = "sdk/"+str(p.relative_to(SDK/"include"))
            else:
                continue
            originals[rel] = p
    metadata["headers"] = {f:digest(p) for f,p in originals.items()}
    metadata["kernel_sha256"] = digest(work/"release"/("lib"+name+".so"))
    return name,paths,metadata,originals
