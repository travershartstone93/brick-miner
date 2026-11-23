"""Time the hand-written bindings against exactly the independently specified native examples."""
import json
from pathlib import Path
import sys
from time import monotonic

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"src"))
from artifacts import write
import project_check as checks

BINDINGS = {
    "ascii_radix36_digit_value": ("main.cc", "CHECK_RADIX_VALUE"),
    "uppercase_hex_digit_byte": ("main.cc", "CHECK_HEX_DIGIT"),
    "number_option_prefix": ("arg_parser.cc", "CHECK_NUMBER_PREFIX"),
}

def compare(package, output):
    contracts = json.loads((ROOT/"contracts/project-lzip.json").read_bytes())["contracts"]
    database = json.loads((package/"build/compile_commands.json").read_bytes())
    units = {(Path(c["directory"])/c["file"]).resolve():c for c in database}
    output.mkdir()
    receipt = {}
    for contract in contracts:
        name = contract["id"]
        filename, selection = BINDINGS[name]
        unit = package/"source"/filename
        cmd = units[unit]
        preamble = (f'#define main brick_original_program_main\n#include {json.dumps(str(unit))}\n#undef main\n'
                    f'#define {selection}\n#include {json.dumps(str(ROOT/"comparison/lzip.hpp"))}\n')
        driver = output/(name+".cpp")
        driver.write_text(checks.make_driver(preamble,"reference_binding",contract))
        binary = output/name
        start = monotonic()
        if not checks.compile_cpp(["clang++",*checks.build_oracle_args(cmd,unit),*checks.PROBE_FLAGS,driver,"-o",binary],
                                  cmd["directory"],output/(name+".log")):
            raise RuntimeError(f"Reference binding did not compile: {name}")
        rows = [a for a,w in contract["cases"]]
        want = checks.framed_output([checks.encoded(w) for a,w in contract["cases"]])
        if checks.probe(binary, rows) != want:
            raise RuntimeError(f"Reference binding failed its contract: {name}")
        receipt[name] = {"cases":len(rows),"passed":True, "seconds":round(monotonic()-start,3)}
    write(output/"report.json",receipt)

if __name__ == "__main__":
    compare(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
