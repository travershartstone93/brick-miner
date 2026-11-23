"""Prepare the two upstream translation units and their configure/build prerequisites."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile

ARCHIVE = Path("/opt/cuda/extras/cuda-gdb-13.3.73.src.tar.gz")
ARCHIVE_SHA = "44bd47fab475c83704a9ebaa36d7c849e0783524921176b935ef94319f14537a"
UNITS = ("xml-utils", "common-inferior")

def unpack(root):
    with ARCHIVE.open("rb") as f:
        if hashlib.file_digest(f, "sha256").hexdigest() != ARCHIVE_SHA:
            raise ValueError("CUDA-GDB archive differs from the frozen input")
    needed = set("""
        COPYING3 install-sh config.guess config.sub config.rpath missing depcomp compile ar-lib
        bfd/development.sh gnulib/configure gnulib/config.in gnulib/Makefile.in
        gnulib/import/stdio.in.h gnulib/import/c++defs.h gnulib/import/arg-nonnull.h
        gnulib/import/warn-on-use.h gnulib/import/pathmax.h gnulib/import/Makefile.in gnulib/import/memmem.c gnulib/Makefile.gnulib.inc.in
        gdbsupport/configure gdbsupport/config.in gdbsupport/Makefile.in
        gdbsupport/xml-utils.cc gdbsupport/common-inferior.cc
        include/ansidecl.h include/libiberty.h include/hashtab.h include/partition.h
        include/safe-ctype.h include/filenames.h include/gdb/signals.h include/gdb/signals.def
        include/dwarf2.h include/dwarf2.def gdb/defs.h
        """.split())
    # Only the support module's interface files are inventoried. No full debugger checkout.
    with tarfile.open(ARCHIVE, "r|gz") as archive:
        for member in archive:
            name = member.name.removeprefix("cuda-gdb-13.3.73/")
            if not member.isfile() or (name not in needed and
                    not (Path(name).parent == Path("gdbsupport") and name.endswith((".h", ".def")))):
                continue
            dest = root / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            with dest.open("xb") as out:
                shutil.copyfileobj(archive.extractfile(member), out)
            dest.chmod(member.mode & 0o755)

def configure_and_capture(work):
    # The outer runner supplies a stable path without spaces for Autoconf.
    root = work / "input"
    unpack(root)
    log = work / "gdb-build.log"
    def execute(args, cwd):
        with log.open("ab") as out:
            subprocess.run(args, cwd=cwd, stdout=out, stderr=subprocess.STDOUT, check=True)
    for part in ("gnulib", "gdbsupport"):
        build = work / "build" / part
        build.mkdir(parents=True)
        config = ["../../input/" + part + "/configure", "--disable-dependency-tracking"]
        if part == "gdbsupport":
            config.extend(["CXX=clang++", "--disable-werror"])
        execute(config, build)
    execute(["make", "stdio.h"], work / "build/gnulib/import")
    records = []
    for unit in UNITS:
        execute(["make", "V=1", "CXXFLAGS=-O2 -MJ " + unit + ".command", unit + ".o"], build)
        raw = (build / (unit + ".command")).read_text()
        records.append(json.loads(raw.rstrip().removesuffix(",")))
    (work / "compile_commands.json").write_text(json.dumps(records, indent=2) + "\n")
