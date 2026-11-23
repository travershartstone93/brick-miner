"""Qualify the reusable row operations in one fresh run directory."""
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from time import monotonic

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "src"))
import trial as pilot

WORK = Path.cwd()
SUITE = pilot.TRIAL / "stress/end-user-100"
SOURCE = json.loads((HERE / "sources.json").read_bytes())
CONTRACTS = json.loads((HERE / "contracts.json").read_bytes())
CASES = json.loads((HERE / "tests/cases.json").read_bytes())
PACKAGE = Path("/var/cache/pacman/pkg") / SOURCE["archive"]
TO_PREFIX = "brick_miner::records::"
CPP = ["g++-15", "-std=c++17", "-O1", "-fPIC", "-Wall", "-Wextra", "-Werror"]
MAIN = """#include "component.hpp"
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    try { component_0(argv[1],argv[2]); } catch (const std::exception&) { return 1; }
}
"""


def audit_requests():
    requests = (SUITE / "REQUESTS.md").read_bytes()
    pilot.check(pilot.sha(requests) == "c1cbea642d46a3411ec6a49cf2c17e23d31f4a365242fa6287dd5c48734f06a5", "Request suite changed")
    domains = dict(re.findall(r"^## (H\d+) · (.+)$", requests.decode(), re.M))
    with (HERE / "request-map.tsv").open() as file:
        mapped = list(csv.DictReader(file, delimiter="\t"))
    pilot.check([r["request"] for r in mapped] == list(domains) and len(domains) == 100, "Incomplete request map")
    result = {}
    for code, key in zip("GAP", ("group_rows", "aggregate_groups", "select_group_row")):
        ids = [r["request"] for r in mapped if code in r["bricks"]]
        result[key] = {"requests":ids, "workflows":sorted({domains[i] for i in ids})}
    return result


def package_input():
    pilot.check(pilot.sha(PACKAGE.read_bytes()) == SOURCE["sha256"], "Upstream package changed")
    keyring = WORK / "archlinux.gpg"
    pilot.run(["gpg","--batch","--dearmor","--output",keyring,"/usr/share/pacman/keyrings/archlinux.gpg"])
    status = pilot.run(["gpgv","--keyring",keyring,"--status-fd","1",str(PACKAGE)+".sig",PACKAGE],
                       stderr=subprocess.PIPE)
    signer, = re.findall(rb"^\[GNUPG:\] VALIDSIG (\w+) ", status, re.M)
    signer = signer.decode("ascii")
    info = pilot.run(["bsdtar","-xOf",PACKAGE,".BUILDINFO"]).decode()
    pilot.check("pkgbuild_sha256sum = "+ SOURCE["pkgbuild_sha256"] in info.splitlines(), "Wrong packaging revision")
    provenance, evidence = {}, {}
    bits = Path(SOURCE["include_dir"])
    for key, symbols in SOURCE["operations"].items():
        files = {}
        for name, functions in symbols.items():
            path = bits / name
            raw = pilot.run(["bsdtar","-xOf",PACKAGE,str(path)[1:]])
            pilot.check(raw == path.read_bytes(), f"Installed header differs from Arch package: {name}")
            material = raw.decode()
            for fn in functions:
                pilot.check(re.search(r"\b"+fn+r"\s*\(", material), f"Missing upstream operation {fn}")
            files[str(path)] = {"sha256":pilot.sha(raw),"symbols":functions}
            evidence["notices/" + name + ".txt"] = material.split("#ifndef",1)[0].encode()
        provenance[key] = {k:SOURCE[k] for k in ("package","release","upstream_revision","archive","sha256","pkgbuild_sha256")}
        provenance[key].update(files=files, signing_key=signer, binding="project-authored C++17 adapter",
                               build_flags=["-std=c++17"], runtime="libstdc++", license="GPL-3.0-or-later WITH GCC-exception-3.1")
    evidence["COPYING3"] = Path("/usr/share/licenses/spdx/GPL-3.0-or-later.txt").read_bytes()
    evidence["COPYING.RUNTIME"] = Path("/usr/share/licenses/gcc/RUNTIME.LIBRARY.EXCEPTION").read_bytes()
    return provenance, evidence


def spec_for(case):
    operations = []
    if case.get("legacy"):
        ops = [("ordered_column", {"column":1}), ("reverse_order", {}), ("unique_lines", {})]
    else:
        ops = []
        for name, field in [("group_rows","group"),("aggregate_groups","aggregate"),("select_group_row","select")]:
            if field in case:
                ops.append((name, dict(zip(CONTRACTS[name]["args"], case[field], strict=True))))
        ops.append(("ordered_column", {"column":case["column"]}))
    for name, params in ops:
        operations.append({"outcome":name, "parameters":params})
    return {"input_type":"text/tab-separated-values","roles":[{"id":"r","content":"whole","multiple":False,
            "output_type":case.get("format","application/json"),
            "requirement":{"operations":operations,"uncovered":[]}}]}


def emit(bundle, dest, spec):
    dest.mkdir(parents=True)
    return pilot.plan(bundle, "", dest, spec=spec)


def check_emitted(bundle, case, folder, spec):
    emission = emit(bundle, folder, spec)
    provision, = emission["providers"]
    pilot.check(not emission["gaps"] and provision["status"] == "complete" and not provision["uncovered"],
                f"Incomplete plan for {case['name']}: {emission}")
    for op in spec["roles"][0]["requirement"]["operations"]:
        key = op["outcome"]
        if key in CONTRACTS:
            pilot.check(pilot.card_id(key) in provision["bricks"] and TO_PREFIX + key + "(" in provision["source"],
                        "Emitter failed to use the selected brick")
    (folder / "component.hpp").write_text(provision["source"])
    (folder / "main.cpp").write_text(MAIN)
    helpers = json.loads((folder/"components/helpers.json").read_bytes())
    packages = sorted({pkg for h in helpers for pkg in h["packages"]})
    binary = folder / "program"
    pilot.run([*CPP,"-I",folder/"bricks",folder/"main.cpp","-o",binary,*pilot.flags(packages)])
    for label in ("ordinary","empty","missing"):
        dest = folder / (label + ".out")
        path = folder / label
        if label != "missing":
            path.write_text(CASES["data"][case["data"]] if label == "ordinary" else "")
        proc = subprocess.run([binary,path,dest],timeout=10)
        if label == "missing":
            pilot.check(proc.returncode == 1 and not dest.exists(), "Missing-input check failed")
        else:
            pilot.check(proc.returncode == 0, f"{case['name']} failed on {label}")
            out = (dest.read_text().splitlines() if case.get("format") == "text/plain"
                   else json.loads(dest.read_bytes()))
            pilot.check(out == (case["expected"] if label == "ordinary" else []), f"Wrong output: {out}")
    return {"bricks":provision["bricks"],"source":provision["source"],"checks":["ordinary","empty","missing"]}


def benchmark(request, binary, dest):
    reports = {}
    for label, options in [("development",[]),("held",["held"])]:
        r = json.loads(pilot.run([sys.executable, "-B", SUITE/"check.py",binary,dest/label,*options],
                                 env={**os.environ,"HARD_CASE":request,"PYTHONDONTWRITEBYTECODE":"1"}))
        reports.update(r["checks"])
    pilot.check(len(reports) == 5 and all(r["pass_"] for r in reports.values()), f"{request} failed: {reports}")
    return reports


def reject_complete(emission):
    pilot.check(not emission.get("interpretation_errors") and
                not any(p["status"] == "complete" for p in emission["providers"]), "Control unexpectedly solved")


def check_gcc_headers(metadata, dependency_file):
    dependencies = dependency_file.read_text()
    for data in metadata.values():
        for file in data["files"]:
            pilot.check(str(Path(file).resolve()) in dependencies or file in dependencies,
                        f"Header unused: {file}")


def qualify(*, adapter_dir=HERE/"include", acquire=package_input, packages=(), origin="authored",
            check_client=check_gcc_headers, extra_payloads=None):
    start = monotonic()
    coverage = audit_requests()
    base = pilot.load_base()
    metadata, notices = acquire()
    headers = {k:(adapter_dir/(pilot.card_id(k)+".hpp")).read_bytes() for k in CONTRACTS}
    tasks = {k:{**v,"call":TO_PREFIX+k,"packages":list(packages), "origin":origin} for k,v in CONTRACTS.items()}
    conversion = {"id":"flatten_row_groups","brick":pilot.card_id("group_rows"),"from":"row_groups","to":"rows",
                  "call":TO_PREFIX+"flatten_groups","header":"miner_group_rows.hpp"}
    stage = pilot.arm_bundle(base, headers, metadata, tasks=tasks,
                             port_types={"row_groups":TO_PREFIX+"Groups"}, conversions=[conversion])
    acquisition = monotonic() - start
    # Run an independent C++ client against the public API and prove which package headers the compiler used.
    work_headers = WORK/"client-headers"
    work_headers.mkdir()
    for k,v in headers.items():
        (work_headers/(pilot.card_id(k)+".hpp")).write_bytes(v)
    cflags = ["-I",work_headers,"-I",pilot.BASE/"providers","-I",pilot.BASE/"headers"]
    pilot.run([*CPP,*cflags,"-MD","-MF",WORK/"used.d",HERE/"tests/client.cpp","-o",WORK/"client",*pilot.flags(packages)])
    pilot.run([WORK/"client"])
    check_client(metadata, WORK/"used.d")

    receipts = {}
    for case in CASES["programs"]:
        name = case["name"]
        folder = WORK/"programs"/name
        spec = spec_for(case)
        baseline = emit(base,folder/"baseline",spec)
        reject_complete(baseline)
        result = check_emitted(stage,case,folder/"staged",spec)
        removed = {}
        for op in spec["roles"][0]["requirement"]["operations"]:
            key = op["outcome"]
            if key in CONTRACTS:
                control = emit(pilot.ablate(stage,key),folder/("without-"+key),spec)
                reject_complete(control)
                removed[key] = {"gaps":control["gaps"],"provider_status":[p["status"] for p in control["providers"]]}
        receipts[name] = {"spec":spec,"spec_sha256":pilot.sha(pilot.encode(spec)),"baseline":baseline["gaps"],
                          "staged":result, "removed":removed}
    # H010 also has an existing alternative with the original catalogue.
    example = next(c for c in CASES["programs"] if c["name"] == "H010")
    check_emitted(base,example,WORK/"H010-legacy",spec_for({**example,"legacy":True}))
    hard = {"H010":benchmark("H010",WORK/"programs/H010/staged/program",WORK/"original-H010"),
            "H010_legacy":benchmark("H010",WORK/"H010-legacy/program",WORK/"legacy-check")}
    for request in ("H032","H084"):
        binary = WORK/request
        defines = ["-DSENSOR_PEAK"] if request == "H032" else []
        pilot.run([*CPP,*cflags,*defines,HERE/"tests/requests.cpp","-o",binary,*pilot.flags(["json-c", *packages])])
        hard[request] = benchmark(request,binary,WORK/("original-"+request))
    for name, contents in notices.items():
        stage["qualifications/row-operations/"+name] = contents
    for name in ["contracts.json","tests/cases.json","tests/client.cpp","tests/requests.cpp"]:
        stage["qualifications/row-operations/"+name] = (HERE/name).read_bytes()
    for key in CONTRACTS:
        loc = "qualifications/"+pilot.card_id(key)+"/"
        stage[loc+"provenance.json"] = pilot.encode(metadata[key])
        stage[loc+"receipt.json"] = pilot.encode({"header_sha256":pilot.sha(headers[key]),"independent_client":"passed",
                "programs":{n:r for n,r in receipts.items() if key in r["removed"]}})
    if extra_payloads is not None:
        stage.update(extra_payloads())
    manifest = pilot.finish_manifest(stage)
    proposed = WORK/".qualifying"
    proposed.mkdir()
    pilot.write_bundle(stage,proposed)
    pilot.run(["sha256sum","--strict","--check","--quiet","SHA256SUMS"],cwd=proposed)
    proposed.rename(WORK/"bundle")
    report = {"base":str(pilot.BASE),"base_verification_sha256":pilot.PINS["verification.json"],
              "base_tree_sha256":pilot.PINS["base_tree_sha256"],"manifest_sha256":manifest,
              "base_cards":len(json.loads(base["catalog.json"])),"staged_cards":len(json.loads(stage["catalog.json"])),
              "sources":metadata,"coverage":coverage,"programs":receipts,"end_user_evaluator":hard,
              "client":"passed on stock and sensor data","seconds":{"acquisition":round(acquisition,3),
              "total":round(monotonic()-start,3)}}
    pilot.write(WORK/"report.json",report)
    print(f"Verified {len(receipts)} emitted programs and 4 benchmark clients. Manifest {manifest}",flush=True)


if __name__ == "__main__":
    qualify()
