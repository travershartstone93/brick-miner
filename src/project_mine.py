"""Project-wide inventory, extraction, differential checks and optional admission."""
from collections import Counter, defaultdict
import json
import os
from pathlib import Path
import re
import subprocess
from time import monotonic

import project_check as tests
from artifacts import sha, write

ROOT = Path(__file__).resolve().parents[1]
OUTCOME_ID = re.compile(r"[a-z][a-z0-9_]*")

def project_file(root, name):
    path = (root/name).resolve(strict=True)
    if not path.is_relative_to(root) or Path(name).is_absolute():
        raise ValueError(f"File outside source root: {name}")
    return path

def inputs(root, db):
    units = {}
    skipped = []
    for cmd in json.loads((db/"compile_commands.json").read_bytes()):
        p = (Path(cmd["directory"])/cmd["file"]).resolve(strict=True)
        if p.suffix not in {".C",".cc",".cpp",".cxx",".c++",".CPP"}:
            skipped.append(p.name)
            continue
        relative = p.relative_to(root).as_posix()
        if relative in units:
            raise ValueError(f"Ambiguous compilation commands for {relative}")
        units[relative] = cmd
    if not units:
        raise ValueError("Compilation database has no C++ translation units under the source root")
    return units, sorted(skipped)

def contract_inputs(path):
    contracts = json.loads(path.read_bytes())["contracts"] if path else []
    ids = set()
    for c in contracts:
        k = c["id"]
        if not OUTCOME_ID.fullmatch(k) or k in ids or not c["cases"] or not c["parameters"]:
            raise ValueError(f"Invalid, empty or duplicate contract: {k}")
        ids.add(k)
        for a,want in c["cases"]:
            if len(a) != len(c["parameters"]) or not isinstance(want,(str,int)):
                raise ValueError(f"Invalid example in {k}")
    return contracts

def source_pins(root, path):
    if not path:
        return None
    p = json.loads(path.read_bytes())
    if not p.get("package") or not p.get("archive_sha256") or not p.get("license_files"):
        raise ValueError("Missing package provenance")
    for f,expected in p["file_evidence"].items():
        if sha(project_file(root,f).read_bytes()) != expected["sha256"]:
            raise ValueError(f"Changed packaged source: {f}")
    for f in p["license_files"]:
        if sha(project_file(root,f).read_bytes()) != p["license_sha256"][f]:
            raise ValueError(f"Changed license: {f}")
    return p

def link_records(records):
    # The compiler supplies a USR for declarations with external linkage. Function names
    # alone cannot distinguish overloads, namespaces or translation-unit-local helpers.
    index = defaultdict(list)
    for record in records:
        if "usr" in record:
            index[record["usr"]].append(record)

    def assemble(root):
        ordered = []
        edges = []
        visiting = set()
        found = {}

        def visit(record):
            key = (record["source"], record["line"], record["column"])
            if key in found:
                return found[key]
            if key in visiting:
                raise ValueError("cyclic cross-unit dependency at " + record["symbol"])
            if record["status"] not in ("extracted", "dependency"):
                raise ValueError(record["symbol"] + ": " + record["reason"])
            visiting.add(key)
            dependencies = []
            for edge in record.get("imports", []):
                matches = index[edge["usr"]]
                if len(matches) != 1:
                    raise ValueError(f"{len(matches)} project definitions for " + edge["symbol"])
                dependencies.append((edge, visit(matches[0])))
            visiting.remove(key)
            found[key] = len(ordered)
            ordered.append(record)
            edges.append(dependencies)
            return found[key]

        entry = visit(root)
        # Out-of-line methods on the same original class must share one type, even
        # when each implementation came from a different translation unit.
        parent = list(range(len(ordered)))
        def group(i):
            while parent[i] != i:
                i = parent[i]
            return i

        types = {}
        for n, record in enumerate(ordered):
            for source, fragment in zip(record["slices"], record["fragments"], strict=True):
                if fragment["kind"] == "type":
                    address = (source["source"], source["offset"])
                    parent[group(n)] = group(types.setdefault(address,n))
        modules = defaultdict(list)
        for n in range(len(ordered)):
            modules[group(n)].append(n)

        output = []
        rendered = set()
        active = set()
        def module_name(n):
            return "project_piece_" + str(group(n))

        def render(n):
            n = group(n)
            if n in rendered:
                return
            if n in active:
                raise ValueError("cyclic dependency between source modules")
            active.add(n)
            imports = {}
            chunks = {}
            symbols = {}
            for member in modules[n]:
                for edge, target in edges[member]:
                    if group(target) == n:
                        continue
                    render(target)
                    scope = edge["scope"]
                    qualified = "::".join([module_name(target), *scope, edge["name"]])
                    using = "using " + qualified + ";"
                    if scope:
                        using = "namespace " + "::".join(scope) + " { " + using + " }"
                    imports[using] = None
                record = ordered[member]
                for source, fragment in zip(record["slices"],record["fragments"],strict=True):
                    address = (source["source"],source["offset"])
                    if symbols.setdefault(source["symbol"], source["source"]) != source["source"]:
                        raise ValueError("cannot merge same-name helpers into one class scope: " + source["symbol"])
                    if chunks.setdefault(address, fragment["code"]) != fragment["code"]:
                        raise ValueError("inconsistent compiled definitions of " + source["symbol"])
            output.extend([f"namespace {module_name(n)} {{", *imports, *chunks.values(), "}"])
            active.remove(n)
            rendered.add(n)

        render(entry)
        return dict(root, original_callable=root["callable"],
                    callable=module_name(entry)+"::"+root["callable"],
                    definitions="\n".join(output)+"\n",
                    files=sorted({f for record in ordered for f in record["files"]}),
                    includes=sorted({h for record in ordered for h in record["includes"]}),
                    closure=list(dict.fromkeys(s for record in ordered for s in record["closure"])),
                    slices=[s for record in ordered for s in record["slices"]],
                    oracle_units=sorted({record["source"] for record in ordered}))

    linked = []
    for record in records:
        if record["status"] == "dependency":
            fields = {k:v for k,v in record.items() if k not in
                      ("definitions","files","includes","closure","slices","callable")}
            linked.append(dict(fields,status="rejected"))
            continue
        if not record.get("imports"):
            linked.append(record)
            continue
        try:
            linked.append(assemble(record))
        except ValueError as error:
            rejection = {k:v for k,v in record.items() if k not in
                         ("definitions","files","includes","closure","slices","callable")}
            linked.append(dict(rejection,status="rejected",reason=str(error)))
    for record in linked:
        record.pop("fragments",None)
    return linked


def scan(root, db, units):
    result = []
    for name in sorted(units):
        result.extend(json.loads(subprocess.check_output([ROOT/".build/brick-scan",db,root/name,root],timeout=120)))
    return sorted(link_records(result),key=lambda r:(r["source"],r["line"],r["column"],r["signature"]))

def verified_header(root, rec, provenance):
    notices = []
    for s in rec["slices"]:
        raw = project_file(root,s["source"]).read_bytes()
        original = raw[s["offset"]:s["offset"]+s["length"]]
        if not original or original not in rec["definitions"].encode():
            raise ValueError("Extracted source slice differs from original bytes")
    if provenance:
        for f in rec["files"]:
            item = provenance["file_evidence"].get(f)
            if not item:
                raise ValueError("Missing source evidence: "+f)
            notice = item["notice"]
            if notice not in notices:
                notices.append(notice)
    return "".join(s+"\n" for s in notices) + tests.native_header(rec)

def exercise(rec, root, units, work, contracts):
    issue = tests.make_clients(rec,root,units,work)
    if issue:
        return {"status":"build failed","reason":issue}, []
    samples = tests.samples(rec["parameters"])
    def both(rows):
        return [tests.probe(work/(p+"-client"),rows) for p in ("original","extracted")]
    observed,candidate = both(samples)
    if not tests.complete_output(observed,len(samples)) or observed != candidate:
        return {"status":"probe failed","reason":"original or isolated client failed, timed out or disagreed"}, []
    matched = []
    for c in contracts:
        if (rec["parameters"],rec["return_type"]) != (c["parameters"],c["return_type"]):
            continue
        rows = [a for a,w in c["cases"]]
        expect = tests.framed_output([tests.encoded(w) for a,w in c["cases"]])
        if both(rows) == [expect,expect]:
            matched.append(c["id"])
    return {"status":"matches original on probes","probe_count":len(samples),
            "output_sha256":sha(observed), "contracts_matched":matched}, matched

def mine(root, db, dest, contract_file=None, provenance_file=None):
    contracts = contract_inputs(contract_file)
    provenance = source_pins(root,provenance_file)
    if contracts and not provenance:
        raise ValueError("A package provenance record is required to stage a catalog bundle")
    if contracts:
        volume = Path("/run/media/whisper/UNLLM_CORPUS")
        subprocess.run(["mountpoint","-q",volume],check=True)
        if volume/"unllm/experiments/brick-miner" not in dest.parents:
            raise ValueError("Stage catalog bundles in a fresh run directory on the brick-miner corpus volume")
    units,skipped = inputs(root,db)
    if provenance and set(units) != set(provenance["translation_units"]):
        raise ValueError("Compilation database disagrees with the recorded project build")
    dest.mkdir(parents=True,exist_ok=False)
    os.chdir(dest)
    start = monotonic()
    subprocess.run(["make","--no-print-directory","-C",ROOT,".build/brick-scan"],check=True)
    records = scan(root,db,units)
    scan_seconds = monotonic()-start
    checks, by_id = {}, {}
    matches = {c["id"]:[] for c in contracts}
    for rec in records:
        if rec["status"] == "rejected":
            continue
        key = tests.source_id(rec)
        rec["id"] = key
        work = dest/"candidates"/key
        work.mkdir(parents=True)
        (work/"candidate.hpp").write_text(verified_header(root,rec,provenance))
        # Keep original declarations and definitions together beside each independent object.
        for f in rec["files"]:
            copy = work/"original"/f
            copy.parent.mkdir(parents=True,exist_ok=True)
            copy.write_bytes(project_file(root,f).read_bytes())
        info, accepted = exercise(rec,root,units,work,contracts)
        checks[key] = info
        by_id[key] = rec
        for name in accepted:
            matches[name].append(key)
    write(dest/"inventory.json",records)
    probe_seconds = monotonic()-start-scan_seconds
    qualifications,manifest = {},None
    baseline = None
    if contracts:
        import project_bundle
        import trial
        trial.WORK = dest
        baseline = {"generation":trial.BASE.name,"manifest_sha256":trial.PINS["verification.json"],
                    "cards":len(json.loads((trial.BASE/"catalog.json").read_bytes()))}
        qualifications,manifest = project_bundle.publish(root,dest,provenance,by_id,matches,contracts,units)
    info = {"units":sorted(units), "skipped_non_cxx":skipped, "definitions":len(records),
            "ast_status":dict(Counter(r["status"] for r in records)),"checks":checks,
            "qualifications":qualifications, "bundle_manifest_sha256":manifest,
            "baseline":baseline,
            "contract_sha256":sha(contract_file.read_bytes()) if contract_file else None,
            "inventory_sha256":sha((dest/"inventory.json").read_bytes()),
            "seconds":{"scan_and_build":round(scan_seconds,3),"client_checks":round(probe_seconds,3),
                       "total":round(monotonic()-start,3)}}
    write(dest/"report.json",info)
    print(f"{len(units)} project units, {len(records)} function and method definitions, "
          f"{len(checks)} extracted, {sum(r['status']=='matches original on probes' for r in checks.values())} "
          f"matched their original on probes, {sum(v['status']=='qualified' for v in qualifications.values())} "
          f"planner-qualified. Report {dest/'report.json'}",flush=True)
