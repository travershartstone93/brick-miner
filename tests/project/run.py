#!/usr/bin/env python3
"""Exercise project-wide discovery, isolation, and reproducibility with a fresh application fixture."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CXX = "clang++"
INPUTS = [-2147483648, -7, -1, 0, 1, 6, 12, 19, 2147483647]
ORACLES = {
    "phase_offset": [1, 3, 15, -7, -5, 5, -7, 7, 7],
    "inside_window": [0, 0, 0, 1, 1, 0, 1, 0, 0],
    "tag_level": [-9, -6, -3, 1, 3, 3, 6, 12, 6],
    "through_class": [-11, -2, 17, 4, 11, 21, 16, 44, 30],
    "CycleView::joined": [-8, 0, 18, 4, 10, 20, 14, 40, 28],
    "CycleView::sign": [0, 0, 1, 1, 1, 1, 1, 1, 1],
    "Calibration::adjusted": [-5, -2, 1, 5, 7, 7, 10, 16, 10],
}
REJECTIONS = ["next_count", "shifted_count", "external_read", "previous_read", "cross_bad", "Tally::add"]
RENAME = {
    "phase_offset": "azimuth_903", "inside_window": "accrual_814", "tag_level": "station_477",
    "bucket": "isotope_198", "scale": "matrix_952", "next_count": "lag_833",
    "add_tick":"tock", "through_class": "seasons_345", "Calibration":"OffsetData", "adjusted":"measured", "CycleView":"ClimateData", "joined":"crest", "sign":"flag",
    "shifted_count": "crest_571", "external_read": "pin_776", "previous_read": "roll_054",
}


def run(*args):
    return subprocess.check_output(args, stderr=subprocess.STDOUT, text=True, timeout=120)


def prepare(root, rename, reverse):
    source, build = root / "app", root / "build"
    shutil.copytree(HERE / "fixture", source)
    if rename:
        pattern = re.compile(r"\b(?:" + "|".join(rename) + r")\b")
        for file in [*source.glob("*.cpp"), *source.glob("*.hpp")]:
            file.write_text(pattern.sub(lambda match: rename[match[0]], file.read_text()))
    run("cmake", "-G", "Ninja", "-S", source, "-B", build,
        "-DCMAKE_CXX_COMPILER=" + CXX, "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON")
    run("cmake", "--build", build)
    if reverse:
        db = build / "compile_commands.json"
        db.write_text(json.dumps(list(reversed(json.loads(db.read_text())))))
    return source, build


def run_inputs(binary):
    return subprocess.check_output([binary], input="\n".join(map(str, INPUTS)), text=True, timeout=5)


def client(folder, header, callable_name, receiver_fields=False):
    folder.mkdir(parents=True)
    shutil.copyfile(header, folder / "candidate.hpp")
    cpp = folder / "client.cpp"
    # Numeric limits are passed from Python so the compiler parsing -2147483648
    # as unary minus on a wider type cannot affect overload resolution.
    arguments = "4, input" if receiver_fields else "input"
    cpp.write_text(
        '#include "candidate.hpp"\n#include <iostream>\n'
        f'int main() {{ int input; while (std::cin >> input) std::cout << {callable_name}({arguments}) << "\\n"; }}\n'
    )
    binary = folder / "client"
    run(CXX, "-std=c++17", cpp, "-o", binary)
    return [int(x) for x in run_inputs(binary).splitlines()]


def trial(root, rename, reverse):
    source, build = prepare(root, rename, reverse)
    original = [[int(value) for value in row.split()] for row in run_inputs(build / "cycle_report").splitlines()]
    output = root / "mine"
    run(REPO / "mine-project", "--source", source, "--database", build, "--output", output)
    inventory = (output / "inventory.json").read_bytes()
    records = json.loads(inventory)
    report = json.loads((output / "report.json").read_text())
    assert report["units"] == ["app.cpp", "classes.cpp", "phases.cpp", "tags.cpp"], report["units"]
    assert report["bundle_manifest_sha256"] is None and not report["qualifications"]
    checks = report["checks"]
    for record in records:
        if record["status"] == "extracted":
            assert checks[record["id"]]["status"] == "matches original on probes", (record["symbol"], checks[record["id"]])
            for file in record["files"]:
                retained = output / "candidates" / record["id"] / "original" / file
                assert retained.read_bytes() == (source / file).read_bytes(), retained
    # The standalone consumer cannot rely on checkout includes, build objects, or the original executable.
    shutil.rmtree(source)
    shutil.rmtree(build)
    for symbol in REJECTIONS:
        record, = [r for r in records if r["symbol"] == rename.get(symbol, symbol)]
        assert record["status"] == "rejected" and record["reason"], record
        if symbol == "cross_bad":
            assert "mutable global" in record["reason"], record
    helpers = [r for r in records if r["symbol"] == rename.get("bucket", "bucket")]
    assert {r["source"] for r in helpers} == {"phases.cpp", "tags.cpp"}, helpers
    # Two file-local functions have the same spelling, linkage, and signature.
    assert len({r["id"] for r in helpers}) == 2, helpers
    payloads = {}
    root_ids = {}
    for column, (symbol, expected) in enumerate(ORACLES.items()):
        actual = "::".join(rename.get(part,part) for part in symbol.split("::"))
        record, = [r for r in records if r["symbol"] == actual]
        assert record["status"] == "extracted", record
        assert record["header"] == "cycle.hpp", record
        if symbol == "through_class":
            assert record["oracle_units"] == ["classes.cpp", "phases.cpp", "tags.cpp"], record
            assert rename.get("add_tick","add_tick") in record["closure"], record
        assert rename.get("bucket", "bucket") in record["closure"], record
        candidate = output / "candidates" / record["id"] / "candidate.hpp"
        standalone = client(root / "standalone" / actual, candidate,
                            f'brick_mined::{record["id"]}::invoke', bool(record.get("receiver_fields")))
        upstream = [row[column] for row in original]
        assert standalone == upstream == expected, (symbol, upstream, standalone)
        root_ids[symbol] = record["id"]
    for path in sorted((output / "candidates").glob("*/candidate.hpp")):
        payloads[str(path.relative_to(output))] = path.read_bytes()
    assert len(payloads) >= 7, sorted(payloads)
    return inventory, payloads, root_ids


def main():
    run("make", "-C", REPO, ".build/brick-scan")
    with tempfile.TemporaryDirectory(prefix="project-regression-") as tmp:
        root = Path(tmp)
        first = trial(root / "plain", {}, False)
        repeat = trial(root / "different directory", {}, True)
        assert first == repeat, "relocation or reversed compilation database changed results"
        renamed = trial(root / "renamed", RENAME, False)
        assert all(first[2][name] != renamed[2][name] for name in ORACLES), "stale function identity"
    print("Passed discovery, transitive rejection, overloads, isolated clients, relocation, reordering, and renaming.")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"{error.cmd}\n{error.output}") from error
