"""Turn behavior-checked native functions into typed row providers using the existing planner."""
import json
from pathlib import Path

import project_check as checks
import trial

def row_header(rec, contract, code):
    columns = [f"column_{i+1}" for i in range(len(rec["parameters"]))]
    cols = "".join(", std::size_t " + c for c in columns)
    args = ", ".join(checks.field_expr(t, f"fields.at({c})") for t,c in zip(rec["parameters"],columns))
    call = f"brick_mined::{rec['id']}::invoke({args})"
    text = rec["return_type"] in checks.TEXT
    # Numeric reports use the catalog's signed 64-bit port. Refuse an unrepresentable value.
    if not text and rec["return_type"] in ("unsigned long", "unsigned long long"):
        call = (f"([&]() -> std::int64_t {{ auto n = {call}; "
                "if (n > INT64_MAX) throw std::overflow_error(\"output exceeds INT64_MAX\"); return n; })()")
    outtype = "std::string" if text else "std::int64_t"
    return ('#pragma once\n' + code + '\n#include "provider_types.hpp"\n' + checks.HEADER +
            f"\nnamespace brick_mined {{\nstatic inline std::vector<{outtype}> {contract['id']}"
            f"(const brick_data::Rows& input{cols}) {{\n    std::vector<{outtype}> result;\n"
            f"    input([&](std::int64_t, const brick_data::Fields& fields) {{ result.push_back({call}); }});\n"
            "    return result;\n}\n}\n")

def task_spec(contract, columns):
    return {"input_type":"text/tab-separated-values", "roles":[{"id":"r", "content":"whole","multiple":False,
            "output_type":"application/json","requirement":{"outcome":contract["id"],
                    "parameters":{f"column_{i+1}":col for i,col in enumerate(columns)}, "uncovered":[]}}]}

def planner_test(base,stage,contract,work):
    name = contract["id"]
    n = len(contract["parameters"])
    columns = list(range(1,n+1))
    spec = task_spec(contract,columns)
    evidence = {"task":spec}
    for label, bundle in (("baseline",base), ("staged",stage), ("removed",trial.ablate(stage,name))):
        dest = work / label
        dest.mkdir(parents=True)
        emit = trial.plan(bundle,name,dest,spec=spec)
        complete = [p for p in emit["providers"] if p["status"]=="complete" and not p.get("uncovered")]
        if label != "staged":
            trial.check(not emit.get("interpretation_errors") and not complete, "Unexpected plan in " + label)
            evidence[label] = {"status":"no program", "gaps":emit["gaps"]}
            continue
        p, = complete
        trial.check(trial.card_id(name) in p["bricks"] and "brick_mined::"+name+"(" in p["source"],
                    "Emitter did not use the new callable")
        (dest/"component.hpp").write_text(p["source"])
        (dest/"main.cpp").write_text('#include "component.hpp"\nint main(int argc,char** argv) {\n'
                '    if (argc != 3) return 2;\n    try { component_0(argv[1],argv[2]); }\n'
                '    catch (const std::exception&) { return 1; }\n}\n')
        helpers = json.loads((dest/"components/helpers.json").read_bytes())
        packages = sorted({v for h in helpers for v in h["packages"]})
        trial.run(["c++","-std=c++17","-fPIC","-O1","-I",dest/"bricks",dest/"main.cpp",
                   "-o",dest/"cli",*trial.flags(packages)])
        # Columns are deliberately offset by a caller-owned label. An empty leading field would
        # still be a real record. Include only values representable as single TSV fields.
        rows = [(a,w) for a,w in contract["cases"] if all(not any(c in str(x) for c in "\t\n") for x in a)]
        trial.check(bool(rows), "Contract has no TSV-compatible program test")
        samples = [{"name":"all-cases","input":"".join("\t".join(["record",*(checks.encoded(x).decode() for x in a)])+"\n"
                  for a,w in rows), "output":[w for a,w in rows]},
                  {"name":"empty","input":"","output":[]}, {"name":"missing","error":"missing"},
                  {"name":"unwritable","input":"label\t0\n","error":"destination-directory"}]
        if n:
            samples.append({"name":"missing-column","input":"record\n","error":"invalid-input"})
        if any(t not in checks.TEXT for t in contract["parameters"]):
            samples.append({"name":"malformed-number","input":"record"+"\txyz"*n+"\n","error":"invalid-input"})
        evidence[label] = {"status":"passed","bricks":p["bricks"],
                           "checks":trial.evaluate(dest/"cli",samples,dest,command=checks.sandbox),
                           "emission_sha256":trial.sha(p["source"].encode()),"test_rows":len(rows)}
    return evidence

def publish(root, dest, provenance, records, matches, contracts, units):
    base = trial.load_base()
    active = json.loads(base["providers/contracts.json"])["outcomes"]
    tasks, meta, headers, selected, results = {}, {}, {}, {}, {}
    owners = {}
    for contract in contracts:
        name = contract["id"]
        if name in active or any(out in active for out in contract.get("equivalent_outcomes", [])):
            results[name] = {"status":"already in catalog"}
            continue
        ids = matches[name]
        if not ids:
            results[name] = {"status":"no match"}
            continue
        rec = min((records[i] for i in ids),key=lambda r:(len(r["closure"]),r["source"],r["line"],r["id"]))
        key = rec["id"]
        if key in owners:
            results[name] = {"status":"same implementation matched another contract",
                             "other_contract":owners[key],"candidate":key}
            continue
        owners[key] = name
        files = sorted(set(rec["files"]) | {rec["source"]} | ({rec["header"]} if rec["header"] else set()))
        # Keep sibling declaration units even when an internal definition has no public declaration.
        for ext in (".h",".hpp",".hh",".hxx"):
            sibling = Path(rec["source"]).with_suffix(ext)
            if (root/sibling).is_file():
                files = sorted(set(files) | {sibling.as_posix()})
        code = (dest/"candidates"/key/"candidate.hpp").read_text()
        headers[name] = row_header(rec,contract,code).encode()
        package = {k:v for k,v in provenance.items() if k not in ("file_evidence","translation_units")}
        command = units[rec["source"]]
        argv = [a.replace(str(root),"{source}").replace(command["directory"],"{build}")
                for a in checks.command_args(command)]
        meta[name] = {"route":"compile-database AST extraction","source_package":package,
                      "symbol":rec["symbol"],"signature":rec["signature"],"closure":rec["closure"],
                      "input_files":{f:trial.sha((root/f).read_bytes()) for f in files},
                      "source_slices":rec["slices"],"original_compile_command":argv,
                      "candidate_sha256":trial.sha(code.encode()), "candidate_id":key,
                      "build_flags":["-std=c++17"],"runtime_packages":[]}
        tasks[name] = {"from":"rows","to":"strings" if rec["return_type"] in checks.TEXT else "integers",
                       "call":"brick_mined::"+name,"packages":[],"origin":"mined_application",
                       "args":{f"column_{i+1}":"column" for i in range(len(rec["parameters"]))},
                       "meaning":contract["meaning"]}
        selected[name] = (rec,contract)
    if not selected:
        return results,None
    stage = trial.arm_bundle(base,headers,meta,tasks=tasks)
    for name,(rec,contract) in selected.items():
        receipt = planner_test(base,stage,contract,dest/"planner"/name)
        results[name] = {"status":"qualified","selected":rec["id"],"matching_candidates":matches[name],**receipt}
        prefix = "qualifications/" + trial.card_id(name) + "/"
        stage[prefix+"contract.json"] = trial.encode(contract)
        stage[prefix+"receipt.json"] = trial.encode(results[name])
        stage[prefix+"provenance.json"] = trial.encode(meta[name])
        stage[prefix+"candidate.hpp"] = (dest/"candidates"/rec["id"]/"candidate.hpp").read_bytes()
        for f in sorted(set(meta[name]["input_files"])|set(provenance["license_files"])):
            stage["qualifications/original/"+f] = (root/f).read_bytes()
    manifest = trial.finish_manifest(stage)
    draft = dest/".bundle"
    draft.mkdir()
    trial.write_bundle(stage,draft)
    trial.run(["sha256sum","--check","--strict","--quiet","SHA256SUMS"],cwd=draft)
    draft.rename(dest/"bundle")
    return results,manifest
