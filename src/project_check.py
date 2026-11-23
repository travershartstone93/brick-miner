"""Compile an isolated caller and an oracle against an unmodified source translation unit."""
import hashlib
import json
from pathlib import Path
import resource
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
TEXT = {"const char*", "std::string", "const std::string&"}
HEADER = (HERE / "project_probe.hpp").read_text()
CPP = ["clang++", "-std=c++17", "-O1"]
PROBE_FLAGS = ["-ffunction-sections", "-fdata-sections", "-fsanitize=undefined",
               "-fno-sanitize-recover=all", "-Wl,--gc-sections"]
READ_FIELD = """
std::string next_field() {
    std::string line;
    if (!std::getline(std::cin, line)) throw std::runtime_error("missing size");
    std::string field(brick_probe::number<unsigned int>(line), '\\0');
    if (!std::cin.read(field.data(), field.size()) || std::cin.get() != '\\n')
        throw std::runtime_error("truncated field");
    return field;
}
"""

def source_id(record):
    identity = [record[key] for key in ("source", "symbol", "signature", "definitions", "includes")]
    return "piece_" + hashlib.sha256(json.dumps(identity).encode()).hexdigest()[:24]

def invocation(rec, callable_name, args, bind_receiver):
    fields = rec.get("receiver_fields") if bind_receiver else None
    if fields:
        owner, method = callable_name.rsplit("::", 1)
        receiver = ", ".join(args[:len(fields)])
        callable_name = f"{owner}{{{receiver}}}.{method}"
        args = args[len(fields):]
    return callable_name + "(" + ", ".join(args) + ")"


def native_header(rec):
    includes = "".join(f"#include <{name}>\n" for name in sorted(set(rec["includes"]) | {"string"}))
    params = ", ".join(f"{t} a{i}" for i,t in enumerate(rec["parameters"]))
    args = [f"a{i}" for i in range(len(rec["parameters"]))]
    call = invocation(rec, "upstream::" + rec["callable"], args, True)
    guard = "BRICK_MINER_" + rec["id"].upper() + "_HPP"
    return (f"#ifndef {guard}\n#define {guard}\n{includes}namespace brick_mined::{rec['id']} {{\n"
            "namespace {\nnamespace upstream {\n"
            f"{rec['definitions']}\n}}\ninline {rec['return_type']} invoke({params}) {{\n"
            f"    return {call};\n}}\n}}\n}}\n#endif\n")

def field_expr(kind, variable):
    if kind == "const char*":
        return f"brick_probe::c_string({variable})"
    if kind in TEXT:
        return variable
    return f"brick_probe::number<{kind}>({variable})"

def make_driver(include, caller, rec, bind_receiver=False):
    fields = [f"f{i}" for i in range(len(rec["parameters"]))]
    read = "".join(f"        const std::string {var} = next_field();\n" for var in fields)
    args = [field_expr(t,var) for t,var in zip(rec["parameters"],fields)]
    val = invocation(rec,caller,args,bind_receiver)
    result = val if rec["return_type"] in TEXT else f"std::to_string({val})"
    return include + "\n#include <iostream>\n" + HEADER + READ_FIELD + f"""
int main() try {{
    std::string n;
    if (!std::getline(std::cin, n)) return 2;
    auto count = brick_probe::number<unsigned int>(n);
    for (unsigned int i = 0; i < count; ++i) {{
{read}        std::string result = {result};
        std::cout << result.size() << '\\n' << result << '\\n';
    }}
    return 0;
}} catch (const std::exception&) {{ return 2; }}
"""

def command_args(cmd):
    return cmd.get("arguments") or shlex.split(cmd["command"])

def build_oracle_args(cmd, unit):
    args = iter(command_args(cmd)[1:])
    result = []
    for arg in args:
        if arg in ("-c","-MMD","-MD","-MP","-MG","--"):
            continue
        if arg in ("-MJ","-o","-MF","-MT","-MQ"):
            next(args, None)
            continue
        if arg.startswith(("-MJ", "-MF","-MT","-MQ")) or (
                not arg.startswith("-") and (Path(cmd["directory"]) / arg).resolve() == unit):
            continue
        result.append(arg)
    return result

def compile_cpp(args, cwd, log):
    with log.open("wb") as f:
        try:
            done = subprocess.run(args,cwd=cwd,stdout=f,stderr=subprocess.STDOUT,timeout=120)
        except subprocess.TimeoutExpired:
            f.write(b"Compile timed out\n")
            return False
    return done.returncode == 0

def make_clients(rec, root, units, work):
    unit = (root / rec["source"]).resolve()
    cmd = units[rec["source"]]
    oracle_objects = []
    for n, source in enumerate(rec.get("oracle_units", [])):
        if source == rec["source"]:
            continue
        dep = units[source]
        path = (root / source).resolve()
        obj = work / f"original-dependency-{n}.o"
        flags = build_oracle_args(dep,path)
        # The application can define main in any unit. Each original dependency retains
        # its own macro definitions, include paths and working directory.
        if not compile_cpp(["clang++", *flags, *PROBE_FLAGS[:-1], f"-Dmain=brick_dependency_main_{n}",
                            "-c",path,"-o",obj],dep["directory"],obj.with_suffix(".log")):
            return "original dependency did not compile: " + source
        oracle_objects.append(obj)
    original = json.dumps(str(unit))
    original_header = f'#define main brick_original_program_main\n#include {original}\n#undef main\n'
    isolated = "brick_mined::" + rec["id"] + "::invoke"
    for name, include, fn, args in [
        ("extracted", '#include "candidate.hpp"', isolated, CPP),
        ("original", original_header, rec.get("original_callable",rec["callable"]), ["clang++", *build_oracle_args(cmd, unit)])]:
        driver = work / (name + ".cpp")
        driver.write_text(make_driver(include,fn,rec,bind_receiver=(name=="original")))
        linked = ["-x", "none", *oracle_objects] if name == "original" and oracle_objects else []
        if not compile_cpp([*args, *PROBE_FLAGS, driver, *linked, "-o",work/(name+"-client")],
                           cmd["directory"],work/(name+"-build.log")):
            return name + " client did not compile or link"
    return ""

def samples(kinds):
    def values(t):
        if t in TEXT:
            return ["", "1", "plain", ".5", "A&b", "xyz", "0", "17", "é"]
        if t == "bool":
            return [0,1]
        return [0,1,2,7,10,15,16,31,65] if t.startswith("unsigned") else [0,1,-1,7,-2,15,16,31,65]
    if not kinds:
        return [[]]
    columns = [values(k) for k in kinds]
    return [[col[(i*(2*j+1)+j) % len(col)] for j,col in enumerate(columns)] for i in range(max(map(len,columns)))]

def encoded(value):
    return (str(int(value)) if isinstance(value,bool) else str(value)).encode()

def case_input(rows):
    parts = [str(len(rows)).encode()+b"\n"]
    for row in rows:
        for item in row:
            value = encoded(item)
            parts.extend([str(len(value)).encode()+b"\n",value,b"\n"])
    return b"".join(parts)

def framed_output(values):
    return b"".join(str(len(v)).encode()+b"\n"+v+b"\n" for v in values)

def complete_output(raw, count):
    if raw is None:
        return False
    offset = 0
    for _ in range(count):
        end = raw.find(b"\n", offset, offset + 12)
        if end < 0 or not raw[offset:end].isdigit():
            return False
        offset = end + 1 + int(raw[offset:end])
        if raw[offset:offset + 1] != b"\n":
            return False
        offset += 1
    return offset == len(raw)

def limits():
    for kind,limit in [(resource.RLIMIT_AS, 512*1024**2),(resource.RLIMIT_CPU,3),
                       (resource.RLIMIT_FSIZE,4*1024**2),(resource.RLIMIT_CORE,0)]:
        resource.setrlimit(kind, (limit,limit))

def sandbox(binary, case_dir=None):
    command = ["bwrap","--die-with-parent","--unshare-all","--new-session","--clearenv",
               "--setenv","LC_ALL","C","--ro-bind","/usr","/usr","--symlink","usr/lib","/lib",
               "--symlink","usr/lib","/lib64","--dev","/dev","--tmpfs","/tmp",
               "--ro-bind",binary,"/probe"]
    if case_dir:
        command.extend(["--bind",case_dir,"/case","--chdir","/case"])
    return [*command,"/probe"]

def probe(binary,rows):
    command = sandbox(binary)
    with tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        try:
            proc = subprocess.run(command, input=case_input(rows),stdout=output,stderr=errors,timeout=5,preexec_fn=limits)
        except subprocess.TimeoutExpired:
            return None
        output.seek(0)
        return output.read() if proc.returncode == 0 else None
