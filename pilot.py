#!/usr/bin/env python3
"""Run twice against the pinned Arch inputs, inside fresh isolated directories."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parent
VOLUME = Path("/run/media/whisper/UNLLM_CORPUS")


def run_pair(root, fixture):
    stamp = "trial-"
    pair = Path(tempfile.mkdtemp(prefix=stamp, dir=root))
    started = time.monotonic()
    subprocess.run(["make", "-C", ROOT], check=True)
    build_seconds = time.monotonic() - started
    reports = []
    for n in (1, 2):
        trial = pair / f"run-{n}"
        trial.mkdir()
        # A fixed internal path also avoids Autoconf's space restrictions. Keep the project,
        # baseline, and package payloads read-only. All test writes go to the disposable run.
        argv = ["bwrap", "--die-with-parent", "--ro-bind", "/", "/", "--dev", "/dev", "--proc", "/proc",
                "--tmpfs", "/tmp", "--bind", trial, "/tmp/brick-trial", "--chdir", "/tmp/brick-trial",
                "--setenv", "LC_ALL", "C", "--setenv", "PYTHONHASHSEED", "0",
                "python3", "-B", ROOT / "src/trial.py", "fixture" if fixture else "bundle"]
        subprocess.run(argv, check=True)
        reports.append(json.loads((trial / "report.json").read_bytes()))
    compared = ["manifest_sha256", "header_sha256", "verification_sha256", "tools", "results", "provenance"]
    if any(reports[0][k] != reports[1][k] for k in compared):
        raise RuntimeError(f"Clean repeat differed; inspect {pair}")
    print("Clean repeat matched both candidate headers and the full manifest.", flush=True)
    print(f"Miner build/check: {build_seconds:.3f}s. Pair: {time.monotonic()-started:.3f}s.", flush=True)
    return pair


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", action="store_true",
                        help="use small workspace build fixtures, compare complete bundle bytes in memory and save reports only")
    args = parser.parse_args()
    if args.fixture:
        evidence = ROOT / "evidence"
        evidence.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="qualification-", dir=ROOT / ".build") as temp:
            pair = run_pair(Path(temp), True)
            dest = Path(tempfile.mkdtemp(prefix="fixture-", dir=evidence))
            for n in (1, 2):
                shutil.copyfile(pair / f"run-{n}/report.json", dest / f"run-{n}.json")
            print(f"Retained evidence: {dest}")
    else:
        # Check the exact mount point. Never create a hidden fallback directory on the root filesystem.
        if subprocess.run(["findmnt", "-n", "-M", VOLUME], stdout=subprocess.DEVNULL).returncode:
            parser.exit(2, f"The corpus volume must be mounted at {VOLUME}.\n")
        run_root = VOLUME / "unllm/experiments/brick-miner"
        try:
            run_root.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            parser.exit(2, f"Cannot create the pilot output directory {run_root} ({exc.strerror}).\n")
        if not os.access(run_root, os.W_OK | os.X_OK):
            parser.exit(2, f"The pilot needs write access to {run_root}.\n")
        pair = run_pair(run_root, False)
        print(f"Candidate bundle: {pair}/run-1/bundle")


if __name__ == "__main__":
    (ROOT / ".build").mkdir(exist_ok=True)
    main()
