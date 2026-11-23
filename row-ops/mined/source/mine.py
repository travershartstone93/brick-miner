#!/usr/bin/env python3
"""Mine DuckDB's registered aggregates and physical key partitioner from official Arch inputs."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

from discover import discover

HERE = Path(__file__).resolve().parent
VOLUME = Path("/run/media/whisper/UNLLM_CORPUS")
CORPUS = VOLUME / "unllm/experiments/brick-miner"
PIN = json.loads((HERE / "arch.json").read_bytes())
CONTRACTS = json.loads((HERE / "contracts.json").read_bytes())
TREE = Path("debug/usr/src/debug/duckdb/duckdb")
SDK = Path("package/usr")
CXX = ["clang++", "-std=c++17", "-DNDEBUG", "-O1", "-fPIC", "-ffile-prefix-map=" + str(Path.cwd()) + "=/brick-miner-row-build"]


def run(args, **kwargs):
    return subprocess.check_output(args, timeout=240, **kwargs)


def sha(path):
    with path.open("rb") as src:
        return hashlib.file_digest(src,"sha256").hexdigest()


def record(path, data):
    path.write_text(json.dumps(data, indent=2, ensure_ascii=True, sort_keys=True) + "\n")


def acquire(cache):
    if cache is None:
        cache = Path("downloads")
        cache.mkdir()
        for name in PIN["files"]:
            if name == "PKGBUILD":
                url = f"https://gitlab.archlinux.org/archlinux/packaging/packages/duckdb/-/raw/{PIN['version']}/PKGBUILD"
            else:
                repo = "extra-debug" if name.startswith("duckdb-debug-") else "extra"
                url = f"https://geo.mirror.pkgbuild.com/{repo}/os/x86_64/{name}"
            run(["curl","-fsSL","--retry","2","--proto","=https","--proto-redir","=https","-o",cache/name,url])
    for name, digest in PIN["files"].items():
        if sha(cache/name) != digest:
            raise ValueError("Pinned archive or recipe differs: " + name)
    keyring = Path("archlinux.gpg").resolve()
    run(["gpg","--batch","--dearmor","--output",keyring,"/usr/share/pacman/keyrings/archlinux.gpg"])
    for suffix,target in (("","package"),("-debug","debug")):
        archive = cache/f"duckdb{suffix}-{PIN['version']}-x86_64.pkg.tar.zst"
        status = run(["gpgv","--keyring",keyring,"--status-fd","1",str(archive)+".sig",archive])
        if ("[GNUPG:] VALIDSIG " + PIN["signer"] + " ").encode() not in status:
            raise ValueError("Wrong signing key for " + archive.name)
        metadata = run(["bsdtar","-xOf",archive,".BUILDINFO"]).decode().splitlines()
        if "pkgbuild_sha256sum = " + PIN["files"]["PKGBUILD"] not in metadata:
            raise ValueError("Build did not attest pinned recipe")
        Path(target).mkdir()
        files = ["usr/src/debug/duckdb/duckdb"] if suffix else ["usr"]
        run(["bsdtar","-xf",archive,"-C",target,*files])
    cls = SDK / "bin/duckdb"
    version = json.loads(run([cls,"-json","-c","SELECT library_version, source_id FROM pragma_version()"]))
    if version != [{"library_version":PIN["tag"],"source_id":PIN["source_id"]}]:
        raise ValueError("Package runtime source revision differs")
    shutil.copy2(cache/"PKGBUILD",Path("original/PKGBUILD"))
    shutil.copy2(SDK/"share/licenses/duckdb/LICENSE",Path("original/LICENSE"))


def ast_groups(groups,flags):
    src = Path(groups[0][0])
    cls = groups[0][1]
    prefix = os.path.commonprefix([group[2] for group in groups])
    text = run(["clang++","-std=c++17","-DNDEBUG",*flags,"-fsyntax-only","-Xclang","-ast-dump=json","-Xclang",
                "-ast-dump-filter="+cls+"::"+prefix,TREE/src]).decode().lstrip()
    methods = []
    while text:
        obj,end = json.JSONDecoder().raw_decode(text)
        text = text[end:].lstrip()
        if obj.get("kind") != "CXXMethodDecl":
            continue
        body = next((n for n in obj.get("inner",[]) if n["kind"]=="CompoundStmt"),None)
        if body:
            pending = [body]
            loops = 0
            while pending:
                node = pending.pop()
                loops += node.get("kind") in {"ForStmt","WhileStmt","DoStmt","CXXForRangeStmt"}
                pending.extend(node.get("inner", []))
            methods.append({"method":obj["name"],"symbol":obj["mangledName"],"loop_count":loops})
    candidates = [m for m in methods if m["loop_count"]]
    if len(candidates) != 1:
        raise RuntimeError("Physical partitioning method is ambiguous")
    return src,candidates[0]


def includes(factories):
    roots = {SDK/"include"}
    for fact in factories.values():
        header = Path(fact["header"])
        if not str(header).startswith("src/include/"):
            index = header.parts.index("include")
            roots.add(TREE/Path(*header.parts[:index+1]))
    # DuckDB's internal headers need its own vendored fmt namespace.
    roots.add(TREE/"third_party/fmt/include")
    return ["-I"+str(p) for p in sorted(roots)]


def registration(selected, group):
    headers = set()
    calls = []
    for alias,f in sorted(selected.items()):
        header = f["header"].split("/include/",1)[1]
        headers.add('#include "'+header+'"')
        factory = f"duckdb::{f['cls']}::{f['method']}()"
        if f["return_type"] == "AggregateFunction":
            factory = f"duckdb::AggregateFunctionSet({factory})"
        calls.append(f'        install(context, "{alias}", {factory});')
    template = (HERE/"register.cpp.in").read_text()
    return (template.replace("@HEADERS@","\n".join(sorted(headers)))
            .replace("@INSTALL@","\n".join(calls)).replace("@PARTITION@",group["symbol"]))


def compile_module(build, units, flags, coverage):
    build.mkdir()
    profile = ["-fprofile-instr-generate","-fcoverage-mapping"] if coverage else []
    # Build the release and instrumented versions from the very same source files and common flags.
    base = [*CXX, *flags, *profile]
    def compile_one(source):
        obj = build / (str(source).replace("/","_")+".o")
        run([*base,"-MMD","-MF",obj.with_suffix(".d"),"-c",source,"-o",obj])
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_one,units))
    binding = build/"register.o"
    run([*CXX,*flags,"-MMD","-MF",build/"register.d","-c","register.cpp","-o",binding])
    lib = build/"libbrick_row_kernels.so"
    run(["clang++","-shared",*profile[:1],"-Wl,-Bsymbolic-functions","-Wl,-z,defs",
         "-Wl,-rpath,$ORIGIN","-Wl,-soname,libbrick_row_kernels.so","-o",lib,*objects,binding,"-L.","-lduckdb"])
    return lib


def preserve_headers(objects):
    files = set()
    for dep in objects.glob("*.d"):
        names = shlex.split(dep.read_text().replace("\\\n"," ").split(":",1)[1])
        for name in names:
            source = Path(name)
            if source.is_relative_to(SDK/"include"):
                dest = Path("original/src/include") / source.relative_to(SDK/"include")
            elif source.is_relative_to(TREE):
                dest = Path("original") / source.relative_to(TREE)
            else:
                continue
            files.add(dest)
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,dest)
    return {p.relative_to("original").as_posix():sha(p) for p in sorted(files)}


def proof(build,profile,tracked):
    library = build/"libbrick_row_kernels.so"
    shutil.copyfile("libduckdb.so",build/"libduckdb.so")
    run(["clang++","-DNDEBUG","-std=c++17","-I.","native_client.cpp","-L"+str(build),
         "-lbrick_row_kernels","-lduckdb","-Wl,-rpath,$ORIGIN","-o",build/"client"])
    env = {**os.environ,"LD_LIBRARY_PATH":str(build.resolve())}
    if profile:
        env["LLVM_PROFILE_FILE"] = str(build/"kernel.profraw")
    run([build/"client"],env=env)
    if not profile:
        if Path("default.profraw").exists():
            raise RuntimeError("Release build recorded a runtime profile")
        return
    run(["llvm-profdata","merge","-sparse",build/"kernel.profraw","-o",build/"kernel.profdata"])
    exported = json.loads(run(["llvm-cov","export",library,"-instr-profile="+str(build/"kernel.profdata"),
                               *[TREE / p for p in tracked]]))["data"][0]
    groups = {}
    for f in exported["functions"]:
        if not f["count"]:
            continue
        first = f["filenames"][0]
        match = next((s for s in tracked if first.endswith("/"+s)),None)
        if match:
            groups.setdefault(match,[]).append(f["name"])
    for src,regexes in tracked.items():
        active = groups.get(src,[])
        for regex in regexes:
            if not any(re.search(regex, name) for name in active):
                raise RuntimeError(f"Missing native coverage for {src} {regex}\n{active}")
    return {src:sorted(set(active)) for src,active in sorted(groups.items())}


def main(output,cache):
    run(["mountpoint","-q",VOLUME])
    if CORPUS not in output.parents:
        raise ValueError("Source builds require a fresh directory on the mounted corpus")
    output.mkdir(parents=True)
    os.chdir(output)
    CXX[-1] = f"-ffile-prefix-map={output}=/brick-miner-row-build"
    Path("original").mkdir()
    acquire(cache)
    selected,groups,inventory = discover(TREE,SDK/"bin/duckdb",CONTRACTS)
    flags = includes(selected)
    group_src,group = ast_groups(groups,flags)
    (Path("register.cpp")).write_text(registration(selected,group))
    units = sorted({TREE/f["source"] for f in selected.values()}|{TREE/group_src})
    shutil.copyfile(SDK/"include/duckdb.h","duckdb.h")
    shutil.copyfile(SDK/"lib/libduckdb.so","libduckdb.so")
    shutil.copyfile(HERE/"native_client.cpp","native_client.cpp")
    production = compile_module(Path("build"),units,flags,False)
    shutil.copyfile(production,"libbrick_row_kernels.so")
    headers = preserve_headers(Path("build"))
    for f in selected.values():
        src = TREE/f["header"]
        dest = Path("original")/f["header"]
        if not dest.exists():
            dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(src,dest)
        if sha(dest) != sha(src):
            raise RuntimeError("Header differs between source and SDK")
    tracked = {str(group_src):[re.escape(group["symbol"])]}
    execution = {"brick_count":r"Operation|CountFlatLoop","brick_first":r"FirstFunctionILb0.*(Operation|Finalize)",
                 "brick_last":r"FirstFunctionILb1.*(Operation|Finalize)","brick_list":r"List(Update|Finalize|Scatter)",
                 "brick_sum":r"AddValues|SumSetOperation.*Combine"}
    for alias,expr in execution.items():
        tracked.setdefault(selected[alias]["source"],[]).append(expr)
    proof(Path("build"),False,tracked)
    compile_module(Path("instrumented"),units,flags,True)
    covered = proof(Path("instrumented"),True,tracked)
    for src in units:
        rel = src.relative_to(TREE)
        if not (Path("original")/rel).is_file():
            raise RuntimeError("Original source missing: "+str(rel))
    meta = {"source":PIN,"license":{"file":"original/LICENSE","sha256":sha(Path("original/LICENSE"))},
            "upstream_files":headers,"factories":selected,"aggregate_inventory":inventory,
            "physical_partition":{"source":str(group_src),**group},
            "native_checks":{"all_names_absent_before_registration":True,"production_query_passed":True,
                             "instrumented_query_passed":True,"executed_source_functions":covered},
            "commands":{"compiler":run(["clang++","--version"]).decode().splitlines()[0],
                        "module_flags":[*CXX[:-1],"-ffile-prefix-map=<build>=/brick-miner-row-build",*flags],
                        "translation_units":[str(s.relative_to(TREE)) for s in units]},
            "outputs":{name:sha(Path(name)) for name in ("duckdb.h","libduckdb.so","libbrick_row_kernels.so",
                                                         "register.cpp","native_client.cpp")}}
    record(Path("provenance.json"),meta)
    print(f"Mined {len(selected)} aggregate factories and key partitioning from {len(units)} original source units",flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    main(args.output.resolve(),args.cache.resolve() if args.cache else None)
