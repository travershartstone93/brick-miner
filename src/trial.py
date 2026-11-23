"""One fresh, offline trial. The outer runner handles storage selection and replication."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from time import monotonic

from artifacts import encode, sha, write
from gdb_input import ARCHIVE, ARCHIVE_SHA, configure_and_capture

ROOT = Path(__file__).resolve().parents[1]
REPO = ROOT.parent / "unllm-upgrade"
ACTIVE = Path("/run/media/whisper/UNLLM_CORPUS/unllm/boilerplate/active")
BASE = ACTIVE.resolve(strict=True)
WORK = Path.cwd()
TASKS = json.loads((ROOT / "contracts/tasks.json").read_text())
PINS = json.loads((ROOT / "contracts/inputs.json").read_text())
XXH = Path("/usr/include/xxhash.h")
XXLIB = Path("/usr/lib/libxxhash.so.0.8.4")
XXLIC = Path("/usr/share/licenses/xxhash/LICENSE")
CLIENT = ROOT / "tests/client.cpp"
TOOLS = REPO / "build/native"
TRIAL = REPO / "experiments/boilerplate"
TIMES = {}


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def run(args, **kwargs):
    return subprocess.check_output(args, timeout=120, **kwargs)


@contextmanager
def timed(key):
    start = monotonic()
    try:
        yield
    finally:
        TIMES[key] = round(monotonic() - start, 3)


def manifest_bytes(files):
    return "".join(f"{sha(files[n])}  {n}\n" for n in sorted(files)).encode()


def load_base():
    check(str(BASE).endswith("/" + PINS["base"]), "Active generation changed")
    for name in ("catalog.json", "verification.json"):
        check(sha((BASE / name).read_bytes()) == PINS[name], f"Baseline metadata changed: {name}")
    verify = json.loads((BASE / "verification.json").read_bytes())
    for line in verify["sha256"].splitlines():
        digest, name = line.split("  ", 1)
        check(sha((BASE / name).read_bytes()) == digest, f"Damaged baseline: {name}")
    base = {str(x.relative_to(BASE)): x.read_bytes() for x in sorted(BASE.rglob("*")) if x.is_file()}
    check(sha(manifest_bytes(base)) == PINS["base_tree_sha256"], "Baseline tree changed")
    for name, digest in PINS["planner_files"].items():
        check(sha((REPO / name).read_bytes()) == digest, f"Planner input changed: {name}")
    return base


def check_inputs():
    base = load_base()
    for path, key in [(Path(PINS["xxhash_package"]), "xxhash_package_sha256"),
                      (XXH, "xxhash_header_sha256"), (XXLIB, "xxhash_lib_sha256"),
                      (XXLIC, "xxhash_license_sha256"), (CLIENT, "client_sha256"),
                      (ROOT / "contracts/tasks.json", "tasks_sha256")]:
        check(sha(path.read_bytes()) == PINS[key], f"Pinned input changed: {path}")
    for path in (XXH, XXLIB, XXLIC):
        data = run(["bsdtar", "-xOf", PINS["xxhash_package"], str(path)[1:]])
        check(sha(data) == sha(path.read_bytes()), f"Package payload mismatch: {path}")
    return base


def ast(route, db, unit):
    return json.loads(run([ROOT / ".build/brick-mine", route, db, unit]))


def discover():
    with timed("application.prepare_project"):
        configure_and_capture(WORK)
    with timed("application.match_extract"):
        directory = WORK / "input/gdbsupport"
        app = ast("application", WORK, directory / "xml-utils.cc")
        state = ast("state", WORK, directory / "common-inferior.cc")
        header = (WORK / "build/gdbsupport" / app["header"]).resolve()
        check(header == directory / "xml-utils.h", "Companion header mismatch")
        check(app["closure"] == ["xml_escape_text_append", "xml_escape_text"], "Closure changed")
    with timed("library.match_extract"):
        probe = WORK / "public-header.cpp"
        probe.write_text("#include <xxhash.h>\n")
        db = WORK / "library-cdb"
        db.mkdir()
        write(db / "compile_commands.json", [{"directory": str(WORK), "file": str(probe),
              "arguments": ["clang++", "-std=c++17", "-fparse-all-comments",
                            "-fretain-comments-from-system-headers", "-c", str(probe)]}])
        lib = ast("library", db, probe)
    return {"line_xxh32": lib, "line_xml_escape": app}, state


def materialize(key, info):
    task = TASKS[key]
    check(info["symbol"] == task["symbol"], "API no longer matches frozen contract")
    is_lib = key == "line_xxh32"
    # Preserve the original unit beside its adapted inline dependency closure for the header-only emitter.
    directory = WORK / "input/gdbsupport"
    origin = XXH if is_lib else directory / "xml-utils.cc"
    notice = origin.read_text().split("*/", 1)[0] + "*/\n"
    used = [XXH, XXLIB, XXLIC] if is_lib else [directory / x for x in ("xml-utils.h", "xml-utils.cc")]
    if is_lib:
        provenance = {"package": "extra/xxhash 0.8.4-1", "upstream_release": "v0.8.4",
                      "acquired_archive": Path(PINS["xxhash_package"]).name,
                      "archive_sha256": PINS["xxhash_package_sha256"], "license": "BSD-2-Clause",
                      "pkgbuild_sha256": "22d08718706bc9b4994563c8ac84bc5153819a4d250d468b7b33c0d99481dac2"}
    else:
        provenance = {"package": "extra/cuda 13.3.1-1", "upstream_release": "cuda-gdb-13.3.73",
                      "acquired_archive": ARCHIVE.name, "archive_sha256": ARCHIVE_SHA,
                      "license": "GPL-3.0-or-later", "unit": ["xml-utils.h", "xml-utils.cc"]}
    provenance.update(symbol=info["symbol"], signature=info["signature"],
                      files={x.name: sha(x.read_bytes()) for x in used},
                      cxx_standard="c++17", pkg_config=task["packages"],
                      flags=flags(task["packages"]))
    value = "std::int64_t" if is_lib else "std::string"
    extra = "#include <xxhash.h>" if is_lib else "#include <climits>\n#include <stdexcept>"
    change = "" if is_lib else "// Adapted from GNU GDB on 2026-11-23: reachable definitions are inline and namespaced.\n"
    source = (notice + change + "#pragma once\n#include <cstdint>\n#include <string>\n#include <vector>\n" +
              extra + "\n\nnamespace brick_miner {\n" + info.get("definitions", "") +
              f"inline std::vector<{value}> {key}(const std::vector<std::string>& input) {{\n" +
              f"    std::vector<{value}> out;\n    out.reserve(input.size());\n" +
              "    for (const auto& s : input) {\n        " + info["body"] +
              "\n    }\n    return out;\n}\n} // namespace brick_miner\n")
    return source.encode(), provenance


def flags(packages):
    return shlex.split(run(["pkg-config", "--cflags", "--libs", *packages], text=True)) if packages else []


def card_id(key):
    return "miner_" + key


def arm_bundle(base, generated, metadata, *, manual=False, tasks=None, port_types=None, conversions=()):
    overlay = dict(base)
    cards = json.loads(base["catalog.json"])
    components = json.loads(base["components.json"])
    contracts = json.loads(base["providers/contracts.json"])
    reviews = json.loads(base["reviews.json"])
    for key, task in (TASKS if tasks is None else tasks).items():
        ident = card_id(key)
        check(all(c["id"] != ident for c in cards) and key not in contracts["outcomes"],
              f"Candidate identity already present: {key}")
        desc, header = task["meaning"], ident + ".hpp"
        call = task.get("call", "brick_miner::" + key)
        if manual:
            call = "manual_hash" if key == "line_xxh32" else "manual_escape"
        cards.append({"id": ident, "description": desc, "outputs": [desc], "tags": []})
        components.append(dict(cards[-1], operation=key, header=header, source="headers/" + header,
                               packages=task["packages"], origin=task.get("origin", "mined_application" if key == "line_xml_escape" else "mined_api"),
                               provenance=metadata[key]))
        parameters = task.get("args", {})
        contracts["outcomes"][key] = {"meaning": desc, "parameters": parameters}
        contracts["providers"].append({"id": key, "brick": ident, "from": task.get("from", "strings"),
                                       "to": task["to"], "effect": key, "header": header, "call": call,
                                       "args": list(parameters)})
        overlay["headers/" + header] = generated[key]
    for name, cpp_type in (port_types or {}).items():
        check(name not in contracts["types"], f"Port type already present: {name}")
        contracts["types"][name] = cpp_type
    contracts["providers"].extend(conversions)
    # Run the active generation's existing MIME/outcome indexing step on a scratch file.
    path = WORK / "format-index.json"
    write(path, contracts)
    run([sys.executable, TRIAL / "component-trial/expanded/index-formats.py", path])
    overlay["providers/contracts.json"] = encode(json.loads(path.read_bytes()))
    reviews["catalog"] = cards
    for name, value in [("catalog.json", cards), ("components.json", components), ("reviews.json", reviews)]:
        overlay[name] = encode(value)
    return overlay


def ablate(bundle, key):
    result = dict(bundle)
    # Retain the vocabulary and the other learned operation. Remove only this implementation.
    ident = card_id(key)
    contracts = json.loads(result["providers/contracts.json"])
    contracts["providers"] = [p for p in contracts["providers"] if p["brick"] != ident]
    result["providers/contracts.json"] = encode(contracts)
    for name in ("components.json", "catalog.json"):
        result[name] = encode([c for c in json.loads(result[name]) if c["id"] != ident])
    reviews = json.loads(result["reviews.json"])
    reviews["catalog"] = json.loads(result["catalog.json"])
    result["reviews.json"] = encode(reviews)
    del result["headers/" + ident + ".hpp"]
    return result


def interpretation(key):
    requirement = {"outcome": key, "parameters": {}, "uncovered": []}
    return {"input_type": "text/plain", "roles": [{"id": "r", "content": "whole", "multiple": False,
            "output_type": "application/json", "requirement": requirement}]}


def write_bundle(bundle, root):
    for name, data in bundle.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def plan(bundle, key, dest, *, spec=None):
    if spec is None:
        spec = interpretation(key)
    for sub in ("calls/interpret", "components", "bricks"):
        (dest / sub).mkdir(parents=True)
    write(dest / "calls/interpret/response.json", spec)
    write(dest / "requirements.json", {"r": spec["roles"][0]["requirement"]})
    write(dest / "route.json", {"rows": [{"role": "r", "bound": False}]})
    write(dest / "components/helpers.json", [])
    # The outer process supplies a private tmpfs. The host keeps no provisional bundle.
    with tempfile.TemporaryDirectory(dir="/tmp", prefix="stage-") as tmp:
        write_bundle(bundle, Path(tmp))
        run(["bash", TRIAL / "integrated/provider-stage.sh", tmp], cwd=dest,
            env={**os.environ, "BRICK_PROVIDER_PLANNER": str(TOOLS / "brick-provider-plan"),
                 "BRICK_LOGIC_WORKER": str(TOOLS / "unllm_logic_worker")})
    return json.loads((dest / "components/emission.json").read_bytes())


def independent(key, source, dest, manual, client, packages):
    header = dest / "candidate.hpp"
    header.write_bytes(source)
    is_hash = key == "line_xxh32"
    call = ("manual_hash" if is_hash else "manual_escape") if manual else "brick_miner::" + key
    defines = ["-DAPPLY=" + call] + (["-DTEST_XXH32"] if is_hash else [])
    objects = [WORK / "build/gdbsupport/xml-utils.o"] if manual and not is_hash else []
    run(["c++", "-std=c++17", "-O1", "-fPIC", *defines, "-include", header, client, *objects,
         "-o", dest / "client", *flags(packages)])
    run([dest / "client"], cwd=dest)
    return objects


def evaluate(binary, cases, dest, *, command=None):
    results = []
    for case in cases:
        d = dest / case["name"]
        d.mkdir()
        if "input" in case:
            (d / "in").write_bytes(case["input"].encode())
        elif "input_hex" in case:
            (d / "in").write_bytes(bytes.fromhex(case["input_hex"]))
        error = case.get("error")
        if error == "destination-directory":
            (d / "out").mkdir()
        elif error:
            (d / "out").write_bytes(b"untouched")
        argv = command(binary, d) if command else [binary]
        code = subprocess.run([*argv, "in", "out"], cwd=d, timeout=10).returncode
        if error:
            preserved = (d / "out").is_dir() if error == "destination-directory" else (d / "out").read_bytes() == b"untouched"
            passed = code == 1 and preserved
        else:
            got = json.loads((d / "out").read_bytes()) if code == 0 else None
            passed = got == case["output"] and all(type(a) is type(b) for a,b in zip(got, case["output"]))
        results.append({"case": case["name"], "exit": code, "passed": passed})
    check(all(r["passed"] for r in results), f"Program failed: {results}")
    return results


def trial(bundle, key, arm, *, task=None, client=CLIENT):
    if task is None:
        task = TASKS[key]
    dest = WORK / "checks" / key / arm
    dest.mkdir(parents=True)
    emission = plan(bundle, key, dest)
    report = {"spec_sha256": sha((dest / "calls/interpret/response.json").read_bytes()), "emission": emission}
    if arm in ("baseline", "vocabulary_only", "removed"):
        check(not emission["providers"] and key in emission["gaps"]["r"], f"Unexpected control: {emission}")
        report["status"] = "no program"
        return report
    p, = emission["providers"]
    check(not emission["gaps"] and not emission.get("interpretation_errors") and
          p["status"] == "complete" and not p["typed_gaps"] and not p.get("uncovered") and
          card_id(key) in p["bricks"], "Planner failed to use the candidate")
    manual = arm == "manual"
    header = card_id(key) + ".hpp"
    source = bundle["headers/" + header]
    objects = independent(key, source, dest, manual, client, task["packages"])
    helpers = json.loads((dest / "components/helpers.json").read_bytes())
    packages = sorted({pkg for card in helpers for pkg in card["packages"]})
    check((dest / "bricks" / header).read_bytes() == source, "Stager copied a different header")
    (dest / "component.hpp").write_text(p["source"])
    main = dest / "converter.cpp"
    main.write_text('#include "component.hpp"\nint main(int argc, char** argv) {\n'
                    '    if (argc != 3) return 2;\n    try { component_0(argv[1], argv[2]); }\n'
                    '    catch (const std::exception&) { return 1; }\n}\n')
    binary = dest / "converter"
    run(["c++", "-std=c++17", "-fPIC", "-O1", "-I", dest / "bricks", main, *objects, "-o", binary, *flags(packages)])
    report.update(status="passed", client="passed", checks=evaluate(binary, task["cases"], dest))
    return report


def finalize(bundle, info, metadata, results):
    for key, task in TASKS.items():
        q = "qualifications/" + card_id(key) + "/"
        header = card_id(key) + ".hpp"
        bundle[q + "client.cpp"] = CLIENT.read_bytes()
        bundle[q + "task.json"] = encode(task)
        bundle[q + "spec.json"] = encode(interpretation(key))
        bundle[q + "provenance.json"] = encode(metadata[key])
        bundle[q + "discovery.json"] = encode(info[key])
        bundle[q + "receipt.json"] = encode({
            "header": header, "sha256": sha(bundle["headers/" + header]), "test_sha256": sha(CLIENT.read_bytes()),
            "baseline": results[key]["baseline"]["emission"], "checks": results[key]["staged"]["checks"],
            "bricks": results[key]["staged"]["emission"]["providers"][0]["bricks"],
            "emitted_sha256": sha(results[key]["staged"]["emission"]["providers"][0]["source"].encode()),
            "removed": results[key]["removed"]["emission"], "spec_sha256": results[key]["staged"]["spec_sha256"]})
        if key == "line_xxh32":
            bundle[q + "LICENSE"] = XXLIC.read_bytes()
            bundle[q + "package-info.txt"] = run(["bsdtar", "-xOf", PINS["xxhash_package"], ".PKGINFO"])
        else:
            original = WORK / "input/gdbsupport"
            for name in ("xml-utils.cc", "xml-utils.h"):
                bundle[q + "original/" + name] = (original / name).read_bytes()
            bundle[q + "COPYING3"] = (WORK / "input/COPYING3").read_bytes()
            bundle[q + "compile_command.json"] = encode(json.loads((WORK / "compile_commands.json").read_text())[0])
    return finish_manifest(bundle)


def finish_manifest(bundle):
    components = json.loads(bundle["components.json"])
    names = ["components.json"] + sorted(n for n in bundle if n.startswith("providers/")) + sorted(n for n in bundle if n.startswith("headers/"))
    bundle["verification.json"] = encode({"checked": [c["id"] for c in components],
                                         "sha256": "".join(f"{sha(bundle[n])}  {n}\n" for n in names)})
    manifest = manifest_bytes(bundle)
    bundle["SHA256SUMS"] = manifest
    return sha(manifest)


def verify_denial(bundle, key):
    # Deliberately give the library client a candidate that returns the wrong answer.
    d = WORK / "checks/negative"
    d.mkdir()
    data = bundle["headers/" + card_id(key) + ".hpp"]
    before = b"XXH32(s.data(), s.size(), 0)"
    check(data.count(before) == 1, "Negative control could not locate emitted call")
    try:
        independent(key, data.replace(before, b"0"), d, False, CLIENT, TASKS[key]["packages"])
    except subprocess.CalledProcessError as e:
        check(e.cmd == [d / "client"] and e.returncode == 1, "Negative control failed before executing")
        return
    raise RuntimeError("Independent client admitted deliberately wrong implementation")


def main(mode):
    with timed("verify_inputs"):
        base = check_inputs()
    report = {key: {} for key in TASKS}
    for key in TASKS:
        report[key]["baseline"] = trial(base, key, "baseline")
    discoveries, rejected = discover()
    sources, metadata = {}, {}
    for key, info in discoveries.items():
        with timed(key + ".adapter"):
            sources[key], metadata[key] = materialize(key, info)
    with timed("register"):
        bundle = arm_bundle(base, sources, metadata)
    hand_sources = {key: (ROOT / "comparison" / filename).read_bytes() for key, filename in
                    [("line_xxh32", "xxh32.hpp"), ("line_xml_escape", "xml.hpp")]}
    hand_bundle = arm_bundle(base, hand_sources, metadata, manual=True)
    for key in TASKS:
        for label, variant in [("staged", bundle), ("removed", ablate(bundle, key)), ("manual", hand_bundle)]:
            with timed(key + "." + label):
                report[key][label] = trial(variant, key, label)
        check(len({r["spec_sha256"] for r in report[key].values()}) == 1, "Interpretation changed")
    verify_denial(bundle, "line_xxh32")
    manifest = finalize(bundle, discoveries, metadata, report)
    # Both modes exercise the same write, full readback, checksum verification and rename.
    # Fixture mode publishes only for the lifetime of this private, memory-backed namespace.
    parent = WORK if mode == "bundle" else Path("/tmp")
    pending = parent / ".qualifying"
    pending.mkdir()
    write_bundle(bundle, pending)
    checked = run(["sha256sum", "--strict", "-c", "SHA256SUMS"], cwd=pending).decode().splitlines()
    check(len(checked) == len(bundle) - 1, "Verification omitted files")
    pending.rename(parent / "bundle")
    check(sha((parent / "bundle/SHA256SUMS").read_bytes()) == manifest, "Publication mismatch")
    measured = {"mode": mode, "base": str(BASE), "base_catalog_sha256": sha(base["catalog.json"]),
                "base_verification_sha256": sha(base["verification.json"]),
                "manifest_sha256": manifest, "header_sha256": {k:sha(v) for k,v in sources.items()},
                "verification_sha256": sha(bundle["verification.json"]), "tools": PINS["planner_files"],
                "base_tree_sha256": PINS["base_tree_sha256"],
                "counts": {"examined": 4, "distinct_accepted": 2, "duplicate_entry_points": 1,
                           "rejected": 1, "newly_passing": 2},
                "rejected": rejected, "duplicate": {"symbol":"xml_escape_text_append",
                "reason":"The public wrapper delegates escaping to this helper. Count the pair once."},
                "published_files_checked": len(checked),
                "negative_control": "incorrect XXH32 result rejected by the independent client",
                "provenance": metadata, "results": report, "seconds": TIMES,
                "tool_versions": {x:run([x, "--version"],text=True).splitlines()[0] for x in ("clang++", "c++", "make")},
                "selected_source_hashes": {key:sha((ROOT / "comparison" / name).read_bytes())
                                           for key,name in [("manual_xxh32","xxh32.hpp"),("manual_xml","xml.hpp")]}}
    write(WORK / "report.json", measured)
    print(manifest, flush=True)


if __name__ == "__main__":
    if sys.argv[1:] not in (["fixture"], ["bundle"]):
        raise SystemExit("Expected fixture or bundle")
    main(sys.argv[1])
