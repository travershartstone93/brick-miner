#!/usr/bin/env python3
"""Prepare a complete, pinned Arch application and capture its real compilation database."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

VOLUME = Path("/run/media/whisper/UNLLM_CORPUS")
RUNS = VOLUME / "unllm/experiments/brick-miner"


def digest(path, algorithm="sha256"):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, algorithm).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n")


def acquire(downloads, cache, name, expected, urls):
    target = downloads / name
    if cache:
        shutil.copyfile(cache / name, target)
    else:
        for url in urls:
            proc = subprocess.run(["curl", "-fsSL", "--retry", "1", "--max-time", "45",
                                   "--proto", "=https", "--proto-redir", "=https",
                                   "--output", target, url])
            if proc.returncode == 0 and digest(target) == expected:
                break
        else:
            raise RuntimeError(f"Could not acquire pinned input {name}")
    if digest(target) != expected:
        raise ValueError(f"Incorrect SHA-256 for {name}")
    return target


def collect_inputs(work, cache, pin):
    pkgname = pin["package"].split("/")[1]
    downloads = work / "downloads"
    downloads.mkdir()
    arc = acquire(downloads, cache, pin["archive"], pin["archive_sha256"],
                  [pin["archive_url"], pin["archive_mirror"]])
    recipe = acquire(downloads, cache, pkgname + ".PKGBUILD", pin["pkgbuild_sha256"], [pin["pkgbuild_url"]])
    name = pin["binary_archive"]
    mirrors = ["https://geo.mirror.pkgbuild.com/extra/os/x86_64/" + name,
               f"https://archive.archlinux.org/packages/{pkgname[0]}/{pkgname}/{name}"]
    binary = acquire(downloads, cache, name, pin["binary_sha256"], mirrors)
    signature = acquire(downloads, cache, name + ".sig", pin["signature_sha256"],
                        [u + ".sig" for u in mirrors])
    keyring = downloads / "archlinux.gpg"
    subprocess.run(["gpg", "--batch", "--dearmor", "--output", keyring,
                    "/usr/share/pacman/keyrings/archlinux.gpg"], check=True)
    verification = subprocess.check_output(["gpgv", "--keyring", keyring, "--status-fd", "1",
                                            signature, binary], text=True)
    if f"[GNUPG:] VALIDSIG {pin['package_signer']} " not in verification:
        raise RuntimeError("Arch package signature made by unexpected key")
    buildinfo = subprocess.check_output(["bsdtar", "-xOf", binary, ".BUILDINFO"], text=True)
    if "pkgbuild_sha256sum = " + pin["pkgbuild_sha256"] not in buildinfo.splitlines():
        raise RuntimeError("Signed package does not attest the pinned PKGBUILD")
    algorithm, array = ("blake2b", "b2sums") if "archive_blake2b" in pin else ("sha256", "sha256sums")
    upstream_digest = digest(arc, algorithm)
    hashes = re.search(r"^" + array + r"=\(([^)]*)\)", recipe.read_text(), re.M)
    if (not hashes or upstream_digest != pin["archive_" + algorithm]
            or upstream_digest not in re.findall(r"[a-f0-9]{64,128}", hashes[1])):
        raise RuntimeError("Arch PKGBUILD does not attest the upstream source digest")
    (downloads / "BUILDINFO").write_text(buildinfo)
    return arc


def collect_file_evidence(source):
    evidence = {}
    for file in sorted(source.rglob("*")):
        if file.suffix not in {".cc", ".cpp", ".cxx", ".h", ".hpp"} or not file.is_file():
            continue
        content = file.read_bytes()
        notice = re.match(rb"/\*.*?\*/", content, re.S)
        if not notice or b"Copyright" not in notice[0]:
            raise RuntimeError(f"Source file does not carry an identifiable license notice: {file.name}")
        evidence[file.relative_to(source).as_posix()] = {
            "sha256": hashlib.sha256(content).hexdigest(),
            "notice": notice[0].decode("utf-8"),
        }
    return evidence


def build_project(work):
    source = work / "source"
    build = work / "build"
    build.mkdir()
    fragments = build / "fragments"
    fragments.mkdir()
    env = {**os.environ, "LC_ALL": "C"}
    with (work / "build.log").open("wb") as log:
        for argv in (["../source/configure", "--prefix=/usr", "CXX=clang++",
                      "CXXFLAGS=-O2 -gen-cdb-fragment-path fragments"],
                     ["make", "-j4"], ["make", "check"]):
            subprocess.run(argv, cwd=build, check=True, env=env, stdout=log, stderr=subprocess.STDOUT)
    commands = [json.loads(f.read_text().strip().removesuffix(","))
                for f in fragments.glob("*.json")]
    commands.sort(key=lambda entry: (entry["file"], entry["output"]))
    entries = []
    for entry in commands:
        directory = Path(entry["directory"])
        file = (directory / entry["file"]).resolve(strict=True)
        (directory / entry["output"]).resolve(strict=True)
        entries.append(file.relative_to(source).as_posix())
    if not commands or len(set(entries)) != len(entries):
        raise RuntimeError("Empty or ambiguous compilation database")
    write_json(build / "compile_commands.json", commands)
    return entries


def prepare(work, cache, package):
    pin = json.loads(Path(__file__).with_name(package + ".json").read_text())
    subprocess.run(["mountpoint", "-q", VOLUME], check=True)
    if RUNS not in work.parents:
        raise ValueError(f"Run directory must be under {RUNS}")
    work.mkdir(parents=True, exist_ok=True)
    if any(work.iterdir()):
        raise ValueError(f"Run directory must be empty: {work}")
    archive = collect_inputs(work, cache, pin)
    subprocess.run(["bsdtar", "-xf", archive, "-C", work], check=True)
    source = work / "source"
    (work / (package + "-" + pin["upstream_version"])).rename(source)
    evidence = collect_file_evidence(source)
    units = build_project(work)
    write_json(work / "provenance.json", {
        **pin,
        "license_sha256": {f: digest(source / f) for f in pin["license_files"]},
        "file_evidence": evidence,
        "translation_units": units,
        "compiler": subprocess.check_output(["clang++", "--version"], text=True).splitlines()[0],
        "upstream_check": "passed",
    })
    print(f"Built {pin['package']} {pin['version']} from {len(units)} translation units and passed make check.")
    print(work / "build" / "compile_commands.json")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="fresh output directory beneath the mounted brick-miner corpus")
    parser.add_argument("--cache", type=Path, help="use the downloads directory of a previous run, fully offline")
    parser.add_argument("--package", choices=("lzip", "ocrad"), default="lzip", help="Arch project to build")
    args = parser.parse_args()
    prepare(args.directory.resolve(), args.cache, args.package)
