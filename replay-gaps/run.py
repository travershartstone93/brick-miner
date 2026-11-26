#!/usr/bin/env python3
"""Rank the archived planner holes and qualify a registration for the most frequent one."""
from collections import defaultdict
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
UPSTREAM = ROOT.parent / "unllm-upgrade"
MOUNT = Path("/run/media/whisper/UNLLM_CORPUS")
EXPERIMENTS = MOUNT / "unllm/experiments/brick-miner"
REPLAY = EXPERIMENTS / "english-audit-bcsgf8pi/replay-staged"
REVIEW = EXPERIMENTS / "shared-review-5ui86km8"
BASE_HASH = "e4564e3079f0b32b3379a411b170ce9af61ec80ab7938b6a974e724abd538328"
sys.path[:0] = [str(ROOT), str(ROOT / "src")]
from artifacts import encode, sha, write
from src.trial import finish_manifest, write_bundle
sys.path.insert(0, str(UPSTREAM / "experiments/boilerplate/stress/mined-rows"))
import trial as integrated


def load(path):
    return json.loads(path.read_bytes())


def check(condition, detail):
    if not condition:
        raise ValueError(detail)


def emission(case):
    return load(case / "run/components/emission.json")


def gap_pair(gap):
    return gap["from"], gap["to"]


def affected_slots(plan, target):
    return {p["slot"] for p in plan["providers"]
            if any(gap_pair(g) == target for g in p["typed_gaps"])}


def rank():
    ids = [f"H{i:03}" for i in range(1, 101)]
    check({p.name for p in REPLAY.glob("H*")} == set(ids), "Incomplete replay")
    typed = defaultdict(list)
    stops = defaultdict(set)
    validations = defaultdict(set)
    diagnostics = defaultdict(set)
    for key in ids:
        case = REPLAY / key
        plan = emission(case)
        receipt = load(case / "receipt.json")
        check(receipt["request"] == key and receipt["interpreted"] and not receipt["editor"], key)
        has_hole = False
        for role in plan["providers"]:
            for gap in role["typed_gaps"]:
                has_hole = True
                typed[gap_pair(gap)].append([key, role["slot"], gap["name"]])
        errors = load(case / "run/result.json").get("interpretation_errors", {})
        for message in errors.values():
            validations[message.partition(" expects ")[0]].add(key)
        for message in plan["gaps"].values():
            diagnostics[message].add(key)
        if errors:
            reason = "invalid interpretation"
        elif has_hole:
            reason = "typed conversion gap"
        elif plan["gaps"]:
            reason = ("no ordered outcomes" if all("between one and 32" in msg for msg in plan["gaps"].values())
                      else "no complete chain for the requested operations")
        else:
            check(any(p["uncovered"] for p in plan["providers"]), key)
            reason = "uncovered obligations without a typed gap"
        stops[reason].add(key)
    rows = []
    for (source, dest), occurrences in typed.items():
        keys = sorted({x[0] for x in occurrences})
        rows.append(dict(source=source, destination=dest, requests=keys, count=len(keys),
                         occurrences=occurrences))
    rows.sort(key=lambda r: (-r["count"], r["source"], r["destination"]))
    return dict(typed=rows, stops={k:sorted(v) for k,v in stops.items()},
                validation_parameters={k:sorted(v) for k,v in validations.items()},
                role_diagnostics={k:sorted(v) for k,v in diagnostics.items()})


def qualify(work, bundle, contract):
    schema = load(bundle / "providers/contracts.json")
    components = {c["id"]:c for c in load(bundle / "components.json")}
    target = contract["from"], contract["to"]
    check(not any(gap_pair(p) == target for p in schema["providers"]), "Conversion already present")
    tested = []
    for p in schema["providers"]:
        if (p["from"] != target[0] or not p["to"].startswith("out:")
                or p.get("effect") or p.get("args") or p.get("when")):
            continue
        case = work / p["id"]
        case.mkdir(parents=True)
        args = "path, records" if p.get("destination_first") else "records, path"
        (case / "binding.hpp").write_text(
            f'#include "{p["header"]}"\n#include <string>\n#include <vector>\n'
            'inline void write_candidate(const std::vector<std::string>& records, const char* path) {\n'
            f'    {p["call"]}({args});\n}}\n')
        packages = components[p["brick"]]["packages"]
        flags = (shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", *packages], text=True))
                 if packages else [])
        subprocess.run(["g++-15", "-std=c++17", "-fPIC", "-I", case, "-I", bundle / "headers",
                        "-I", bundle / "providers", HERE / "client.cpp", "-o", case / "client", *flags], check=True)
        checks = {}
        for fixture in contract["cases"]:
            path = case / fixture["name"]
            if fixture.get("error"):
                path.mkdir()
                (path / "marker").write_bytes(b"preserve")
            else:
                path.write_bytes(b"old bytes")
            code = subprocess.run([case / "client", path, *fixture["records"]], timeout=5).returncode
            if fixture.get("error"):
                passed = code == 1 and (path / "marker").read_bytes() == b"preserve"
            else:
                passed = code == 0 and path.read_bytes() == fixture["expected"].encode()
            checks[fixture["name"]] = passed
        tested.append(dict(provider=p, packages=packages, checks=checks, flags=flags,
                           header_sha256=sha((bundle / "headers" / p["header"]).read_bytes())))
    winners = [r["provider"] for r in tested if all(r["checks"].values())]
    check(len(winners) == 1, "Expected exactly one writer meeting the frozen contract")
    new = dict(winners[0], id=contract["provider_id"], to=target[1])
    check(all(p["id"] != new["id"] for p in schema["providers"]), new["id"])
    return new, tested


def build_bundle(dest, base, contract):
    provider, tested = qualify(dest / "clients", base, contract)
    files = {str(p.relative_to(base)):p.read_bytes() for p in base.rglob("*") if p.is_file()}
    del files["SHA256SUMS"]
    schema = json.loads(files["providers/contracts.json"])
    schema["providers"].append(provider)
    files["providers/contracts.json"] = encode(schema)
    prefix = "qualifications/replay-gaps/tsv/"
    for name in ("tsv-contract.json", "client.cpp"):
        files[prefix + name] = (HERE / name).read_bytes()
    files[prefix + "discovery.json"] = encode(dict(base_sha256=BASE_HASH, candidates=tested, registration=provider))
    checksum = finish_manifest(files)
    path = dest / "bundle"
    write_bundle(files, path)
    subprocess.run(["sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"], cwd=path, check=True)
    return path, checksum


def fixture_scores(case):
    receipt = load(case / "receipt.json")
    scores = {**receipt["initial"], **receipt["held"]}
    check(set(scores) == {"ordinary", "boundary", "empty", "transfer", "sparse"}, case)
    return scores


def run():
    check(MOUNT.is_mount(), "Corpus drive is not mounted")
    root = Path(tempfile.mkdtemp(dir=EXPERIMENTS, prefix="ranked-gaps-"))
    print(root, flush=True)
    ranking = rank()
    write(root / "ranking.json", ranking)
    top = ranking["typed"][0]
    contract = load(HERE / "tsv-contract.json")
    target = contract["from"], contract["to"]
    check(target == (top["source"], top["destination"]) and top["count"] > ranking["typed"][1]["count"],
          "The leading gap changed; select its qualification contract")
    base = Path(load(REVIEW / "review.json")["bundle"])
    check(sha((base / "SHA256SUMS").read_bytes()) == BASE_HASH, "Wrong baseline")
    subprocess.run(["sha256sum", "--strict", "--quiet", "-c", "SHA256SUMS"], cwd=base, check=True)
    stage, checksum = build_bundle(root / "run-1", base, contract)
    _, repeat = build_bundle(root / "run-2", base, contract)
    check(checksum == repeat, "Two clean staged manifests differed")

    # Only the writer registration differs in the two catalogs' executable inputs.
    # Replaying the original bundle therefore also ablates just that registration.
    ablated = load(stage / "providers/contracts.json")
    ablated["providers"] = [p for p in ablated["providers"] if p["id"] != contract["provider_id"]]
    check(ablated == load(base / "providers/contracts.json"), "Ablation differs from baseline")
    refs = load(REVIEW / "transfer/transfer.json")
    control_key = "H029"
    rel = Path("run/calls/interpret/response.json")
    control = load(REPLAY / control_key / rel)
    operations, = refs[control_key]["roles"]
    req = control["roles"][0]["requirement"]
    req["operations"] = [dict(outcome=name, parameters=params) for name, params in operations]
    req["uncovered"] = []
    control_path = root / "correct-operations.json"
    write(control_path, control)
    for arm, bundle in (("with-writer", stage), ("without-writer", base)):
        integrated.NEW = bundle
        arm += "-staged"
        def replay(key):
            integrated.execute(key, arm, root, REPLAY / key / rel)
        with ThreadPoolExecutor(max_workers=3) as pool:
            list(pool.map(replay, top["requests"]))
        integrated.execute(control_key, "control-" + arm, root, control_path)

    requested = {}
    for key in top["requests"]:
        before = root / "without-writer-staged" / key
        after = root / "with-writer-staged" / key
        original = REPLAY / key
        slots = affected_slots(emission(before), target)
        check(slots == affected_slots(emission(original), target) and slots, key)
        plan = emission(after)
        bound = {p["slot"] for p in plan["providers"] if any(
            step.get("provider") == contract["provider_id"] for step in p["plan"]["steps"])}
        check(slots == bound and not affected_slots(plan, target), f"Writer not bound in {key}")
        a, b = fixture_scores(before), fixture_scores(after)
        check(a == fixture_scores(original), f"Baseline fixture drift in {key}")
        requested[key] = dict(slots=sorted(slots), before=a, after=b,
                              remaining_typed_gaps=[gap_pair(g) for p in plan["providers"] for g in p["typed_gaps"]],
                              uncovered=[p["uncovered"] for p in plan["providers"]],
                              accepted=load(after / "receipt.json")["verified"])
    ca, cb = (root / ("control-" + arm + "-staged") / control_key for arm in ("without-writer","with-writer"))
    check(affected_slots(emission(ca), target) == {0}, "Control baseline")
    check(load(cb / "receipt.json")["verified"] and not affected_slots(emission(cb), target), "Control staged")
    active = (MOUNT / "unllm/boilerplate/active").resolve(strict=True)
    write(root / "report.json", dict(requests=requested, source_replay=str(REPLAY), base=str(base),
          base_sha256=BASE_HASH, staged_bundle=str(stage), staged_sha256=checksum, repeat_sha256=repeat,
          control={"request":control_key, "before":fixture_scores(ca), "after":fixture_scores(cb)},
          published_catalog=str(active), published_verification_sha256=sha((active / "verification.json").read_bytes())))
    print(root / "report.json", flush=True)


if __name__ == "__main__":
    run()
