"""Mine and qualify one fresh batch against the measured active catalog."""
import json
from pathlib import Path
import re
import shlex
import subprocess
import sys
from time import monotonic
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "src"))
import trial as pilot

TASKS = json.loads((HERE / "tasks.json").read_bytes())
SOURCES = json.loads((HERE / "sources.json").read_bytes())
BEFORE = json.loads((HERE.parent / "evidence/batch2/before.json").read_bytes())
WORK = Path.cwd()


def package_input(src, keys):
    archive = Path("/var/cache/pacman/pkg") / src["archive"]
    pilot.check(pilot.sha(archive.read_bytes()) == src["archive_sha256"], f"Package pin changed: {archive}")
    status = pilot.run(["gpgv", "--keyring", keys, "--status-fd", "1", str(archive) + ".sig", archive],
                       text=True, stderr=subprocess.PIPE)
    signer, = re.findall(r"^\[GNUPG:\] VALIDSIG ([A-F0-9]+) ", status, re.M)
    pkginfo = pilot.run(["bsdtar", "-xOf", archive, ".PKGINFO"])
    buildinfo = pilot.run(["bsdtar", "-xOf", archive, ".BUILDINFO"]).decode().splitlines()
    pilot.check("pkgbuild_sha256sum = " + src["pkgbuild_sha256"] in buildinfo, "PKGBUILD pin changed")
    for name, digest in src["package_files"].items():
        payload = pilot.run(["bsdtar", "-xOf", archive, name.lstrip("/")])
        pilot.check(pilot.sha(payload) == digest == pilot.sha(Path(name).read_bytes()), f"Payload mismatch: {name}")
    licence = Path(src["license_path"])
    pilot.check(pilot.sha(licence.read_bytes()) == src["license_sha256"], "License text changed")
    if licence.parent == Path("/usr/share/licenses") / src["package"]:
        pilot.check(pilot.run(["bsdtar", "-xOf", archive, str(licence)[1:]]) == licence.read_bytes(),
                    "Package notice mismatch")
    return signer, pkginfo


def bindings(src, module):
    flags = shlex.split(pilot.run(["pkg-config", "--cflags", module], text=True))
    raw = pilot.run(["clang++", "-std=c++17", *flags, "-x", "c++", "-fsyntax-only", "-Xclang", "-ast-dump=json",
                     "-Xclang", "-ast-dump-filter=" + src["family"], "-"], text=True,
                    input=f'#include <{src["include"]}>\n')
    found = []
    decoder = json.JSONDecoder()
    while raw.strip():
        raw = raw.lstrip()
        node, end = decoder.raw_decode(raw)
        raw = raw[end:]
        if node["kind"] == "FunctionDecl" and node["loc"].get("file") == src["header"]:
            found.append({"name":node["name"], "signature":node["type"]["qualType"], "line":node["loc"]["line"]})
    selected = {}
    for role, signature in src["signatures"].items():
        matches = [fn for fn in found if fn["signature"] == signature]
        pilot.check(len(matches) == 1, f"Ambiguous or missing {module}/{role}: {matches}")
        selected[role] = matches[0]
    if module == "libsoup-3.0":
        gir = ET.parse("/usr/share/gir-1.0/Soup-3.0.gir")
        for fn in selected.values():
            ident = "{http://www.gtk.org/introspection/c/1.0}identifier"
            entry, = [f for f in gir.iterfind(".//{*}function") if f.get(ident) == fn["name"]]
            pilot.check(entry.find("{*}return-value").get("transfer-ownership") == "full", "Unexpected ownership")
            fn["return_ownership"] = "full"
    return selected


def mine(key, keyring):
    src = SOURCES[key]
    signer, pkginfo = package_input(src, keyring)
    module, = TASKS[key]["packages"]
    selected = bindings(src, module)
    template = (HERE / "adapters" / (key + ".hpp.in")).read_text()
    tokens = set(re.findall(r"@(\w+)@", template))
    pilot.check(tokens == set(selected), "Template bindings do not match the public API")
    header = re.sub(r"@(\w+)@", lambda m: selected[m[1]]["name"], template).encode()
    provenance = {**src, "signing_key":signer, "bindings":selected, "cxx_standard":"c++17",
                  "pkg_config_version":pilot.run(["pkg-config","--modversion",module],text=True).strip(),
                  "flags":pilot.flags([module]), "template_sha256":pilot.sha(template.encode())}
    notice = Path(src["header"]).read_text().split("/*", 1)[1].split("*/", 1)[0]
    evidence = {"provenance.json":pilot.encode(provenance), "package-info.txt":pkginfo,
                "LICENSE":Path(src["license_path"]).read_bytes(), "header-notice.txt":("/*"+notice+"*/\n").encode()}
    return header, provenance, evidence


def qualify(mode):
    base = pilot.load_base()
    pilot.check(pilot.sha((HERE / "tasks.json").read_bytes()) == BEFORE["tasks_sha256"], "Frozen checks changed")
    baseline = {}
    for key, task in TASKS.items():
        baseline[key] = pilot.trial(base, key, "baseline", task=task)
        pilot.check(baseline[key]["emission"] == BEFORE["baseline"][key]["emission"]
                    and pilot.interpretation(key) == BEFORE["baseline"][key]["interpretation"], "Baseline drift")
    print("Active catalog: no program for any of the three frozen tasks.", flush=True)

    keyring = WORK / "archlinux.gpg"
    pilot.run(["gpg", "--batch", "--dearmor", "--output", keyring, "/usr/share/pacman/keyrings/archlinux.gpg"])
    headers, metadata, evidence, seconds = {}, {}, {}, {}
    for key in TASKS:
        start = monotonic()
        headers[key], metadata[key], evidence[key] = mine(key, keyring)
        seconds[key] = {"acquisition":round(monotonic()-start,3)}
    staged = pilot.arm_bundle(base, headers, metadata, tasks=TASKS)
    vocabulary = staged
    for key in TASKS:
        vocabulary = pilot.ablate(vocabulary, key)
    reports = {}
    for key, task in TASKS.items():
        start = monotonic()
        q = {"baseline":baseline[key]}
        client = HERE / "clients" / (key + ".cpp")
        for label, bundle in [("vocabulary_only", vocabulary), ("staged", staged), ("removed", pilot.ablate(staged,key))]:
            q[label] = pilot.trial(bundle, key, label, task=task, client=client)
        for label in ("vocabulary_only","removed"):
            pilot.check("No provider chain" in q[label]["emission"]["gaps"]["r"], f"Unexpected {label} failure")
        plan, = q["staged"]["emission"]["providers"]
        pilot.check("brick_miner::" + key + "(" in plan["source"], "Emitted program did not call mined function")
        pilot.check(len({v["spec_sha256"] for v in q.values()}) == 1, "Interpretation drift")
        reports[key] = q
        seconds[key]["qualification"] = round(monotonic()-start,3)
        print(f'{key}: client passed, {len(q["staged"]["checks"])} CLI checks passed, removal blocked emission.',
              flush=True)
        evidence[key].update({"client.cpp":client.read_bytes(), "task.json":pilot.encode(task),
                            "spec.json":pilot.encode(pilot.interpretation(key)), "receipt.json":pilot.encode({
                                "header":pilot.card_id(key)+".hpp", "sha256":pilot.sha(headers[key]),
                                "test_sha256":pilot.sha(client.read_bytes()), "spec_sha256":q["staged"]["spec_sha256"],
                                "baseline":q["baseline"]["emission"], "vocabulary_only":q["vocabulary_only"]["emission"],
                                "removed":q["removed"]["emission"], "checks":q["staged"]["checks"],
                                "bricks":plan["bricks"], "emitted_sha256":pilot.sha(plan["source"].encode())})})
        for name, content in evidence[key].items():
            staged[f"qualifications/{pilot.card_id(key)}/{name}"] = content

    manifest = pilot.finish_manifest(staged)
    parent = WORK if mode == "bundle" else Path("/tmp")
    proposed = parent / ".qualifying"
    proposed.mkdir()
    pilot.write_bundle(staged, proposed)
    verified = pilot.run(["sha256sum","--strict","-c","SHA256SUMS"],cwd=proposed).splitlines()
    pilot.check(len(verified) == len(staged)-1, "Incomplete manifest")
    proposed.rename(parent / "bundle")
    pilot.check(pilot.sha((parent / "bundle/SHA256SUMS").read_bytes()) == manifest, "Manifest changed")
    summary = {"mode":mode,"base":str(pilot.BASE),"base_tree_sha256":pilot.PINS["base_tree_sha256"],
               "base_catalog_sha256":pilot.sha(base["catalog.json"]),
               "base_verification_sha256":pilot.sha(base["verification.json"]), "tools":pilot.PINS["planner_files"],
               "manifest_sha256":manifest,"header_sha256":{k:pilot.sha(v) for k,v in headers.items()},
               "verification_sha256":pilot.sha(staged["verification.json"]),
               "published_files_checked":len(verified),"results":reports,"provenance":metadata,"seconds":seconds,
               "counts":{"base_cards":len(json.loads(base["catalog.json"])), "staged_cards":len(json.loads(staged["catalog.json"])),
                         "examined_capabilities":len(TASKS), "distinct_accepted":len(headers),
                         "rejected":0,"duplicates":0,"newly_passing":len(reports)}}
    (WORK / "payload.SHA256SUMS").write_bytes(staged["SHA256SUMS"])
    pilot.write(WORK / "report.json",summary)
    print("Manifest",manifest,flush=True)


if __name__ == "__main__":
    if sys.argv[1:] not in (["fixture"],["bundle"]):
        raise SystemExit("expected fixture or bundle")
    qualify(sys.argv[1])
