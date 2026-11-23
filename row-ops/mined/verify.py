"""Qualify the extracted kernels at the original row-operations entry points."""
import json
import os
from pathlib import Path
import subprocess
import sys
from time import monotonic

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import qualify as rows
from qualify import pilot

PKG = "brick-row-kernels"
NATIVE = Path.cwd() / "native"
RUNTIME = Path("/usr/local")
LINKS = {"group_rows": ["brick_list"],
         "aggregate_groups": ["brick_count", "brick_sum"],
         "select_group_row": ["brick_first", "brick_last"]}


def acquisition():
    report = json.loads((NATIVE / "provenance.json").read_bytes())
    report_hash = pilot.sha(pilot.encode(report))
    archive = "qualifications/row-operations/native/provenance.json"
    common = {"route": "compiled modules from signed Arch debug source", "package": "extra/duckdb",
              "release": "1.5.6-1", "native_provenance": archive, "native_provenance_sha256":report_hash,
              "pkg_config": PKG, "runtime": "runtime/duckdb",
              "binding": "project-authored row marshalling and parameterized relational queries"}
    return {op:{**common,"aggregate_names":names} for op,names in LINKS.items()}, {}


def files_below(root, prefix):
    return {f"{prefix}/{p.relative_to(root).as_posix()}":p.read_bytes()
            for p in sorted(root.rglob("*")) if p.is_file()}


def payloads():
    sources = files_below(NATIVE/"original", "qualifications/row-operations/native/original")
    if not sources:
        raise ValueError("No original implementation modules retained")
    sources.update(files_below(RUNTIME, "runtime/duckdb"))
    sources["qualifications/row-operations/native/provenance.json"] = pilot.encode(
        json.loads((NATIVE/"provenance.json").read_bytes()))
    for name in ("native_client.cpp","register.cpp"):
        sources["qualifications/row-operations/native/"+name] = (NATIVE/name).read_bytes()
    for name in ("arch.json","contracts.json","register.cpp.in","discover.py","mine.py"):
        sources["qualifications/row-operations/native/recipe/"+name] = (HERE/"source"/name).read_bytes()
    sources["qualifications/row-operations/request-map.tsv"] = (HERE.parent/"request-map.tsv").read_bytes()
    sources["qualifications/row-operations/comparison.json"] = (Path.cwd()/"comparison.json").read_bytes()
    sources["qualifications/row-operations/tests/differential.cpp"] = (HERE/"tests/differential.cpp").read_bytes()
    return sources


def check_client(metadata, dependency_file):
    binary = Path.cwd()/"client"
    # The independent client and every generated CLI use the bundled ABI.
    needed = pilot.run(["readelf","-d",binary], text=True)
    for lib in ("libduckdb.so","libbrick_row_kernels.so"):
        pilot.check(f"[{lib}]" in needed, f"Missing ELF dependency {lib}")
    pilot.check("duckdb.h" in dependency_file.read_text(), "Independent client did not use DuckDB's header")
    # Prevent a no-op registration hook from silently selecting the stock operators.
    stub = Path.cwd()/"unregistered.cpp"
    stub.write_text('extern "C" void brick_register_row_kernels(void*) {}\n')
    pilot.run(["c++","-fPIC","-shared",stub,"-o",stub.with_suffix(".so")])
    output = subprocess.run([binary], env={**os.environ,"LD_PRELOAD":str(stub.with_suffix(".so"))},
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=10)
    pilot.check(output.returncode != 0 and "brick_list" in output.stderr.lower(),
                f"Disabling registration failed to isolate the extracted kernel: {output.stderr}")


def comparison():
    # Use the original comparison implementation and the exact same independent client.
    output = Path.cwd()/"authored-client"
    start = monotonic()
    pilot.run([*rows.CPP, "-I", HERE.parent/"include", "-I", pilot.BASE/"providers",
               "-I", pilot.BASE/"headers",HERE.parent/"tests/client.cpp", "-o",output])
    pilot.run([output])
    elapsed = monotonic()-start
    source = HERE/"tests/differential.cpp"
    outputs = []
    for name,include,packages in (("original", HERE.parent/"include", []),
                                  ("mined", HERE/"include", [PKG])):
        binary = Path.cwd()/(name+"-differential")
        pilot.run([*rows.CPP, "-I", include, "-I", pilot.BASE/"providers", source, "-o", binary,
                   *pilot.flags(packages)])
        outputs.append(pilot.run([binary]))
    reference_hash = "82fb12db32121bf9552a967d074c36f9992a4567b23c1c6820118b8d9fa864af"
    pilot.check(outputs[0] == outputs[1] and pilot.sha(outputs[1]) == reference_hash,
                "Mined implementation disagreed with the frozen differential oracle")
    receipt = {"implementation":"original hand-written row operations",
               "differential":{"bytes":len(outputs[1]),"sha256":reference_hash,
                               "source_sha256":pilot.sha(source.read_bytes())},
               "client_sha256":pilot.sha((HERE.parent/"tests/client.cpp").read_bytes()), "passed":True}
    pilot.write(Path.cwd()/"comparison.json",receipt)
    pilot.write(Path.cwd()/"comparison-timing.json", {"seconds":round(elapsed,3)})


if __name__ == "__main__":
    comparison()
    rows.qualify(adapter_dir=HERE/"include", acquire=acquisition, packages=[PKG],
                 origin="mined_application", check_client=check_client, extra_payloads=payloads)
