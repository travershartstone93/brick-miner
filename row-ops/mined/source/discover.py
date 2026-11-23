"""Discover native aggregate factories by DuckDB's descriptors, then query their behavior."""
import json
import re
import subprocess
from pathlib import Path


def inventory(tree):
    factories = []
    aliases = {}
    for header in sorted(tree.rglob("*.hpp")):
        text = header.read_text(errors="replace")
        if "static constexpr const char *Name" not in text:
            continue
        for match in re.finditer(r"^struct (\w+) \{(.*?)^\};", text, re.M | re.S):
            cls, body = match.groups()
            name = re.search(r'\*Name = "(\w+)";', body)
            factory = re.search(r"static (AggregateFunction(?:Set)?) (GetFunctions?)\(\);", body)
            if not name:
                continue
            if factory:
                factories.append(dict(name=name[1], cls=cls, method=factory[2], return_type=factory[1],
                                      header=header.relative_to(tree).as_posix()))
            alias = re.search(r"using ALIAS = (\w+);",body)
            if alias:
                aliases[name[1]] = alias[1]
    known = {fact["cls"] for fact in factories}
    return sorted(factories,key=lambda f:f["name"]), {k:v for k,v in aliases.items() if v in known}


def locate(tree, selected, tokens):
    patterns = {f["cls"]: re.compile(r"\b" + f["return_type"] + r"\s+" + f["cls"] + "::" + f["method"] + r"\(")
                for f in selected.values()}
    partition = []
    for base in ("src","extension"):
        for path in sorted((tree/base).rglob("*.cpp")):
            material = path.read_text(errors="replace")
            for fact in selected.values():
                if fact["cls"] in material and patterns[fact["cls"]].search(material):
                    if "source" in fact:
                        raise RuntimeError("More than one implementation of " + fact["cls"])
                    fact["source"] = path.relative_to(tree).as_posix()
            for cls, method in re.findall(r"^\w+\s+(\w+)::(\w+)\s*\(",material,re.M):
                if all(token in method.lower() for token in tokens):
                    partition.append(dict(cls=cls,method=method,source=path.relative_to(tree).as_posix()))
    if any("source" not in f for f in selected.values()) or len({g["source"] for g in partition}) != 1:
        raise RuntimeError("Missing or ambiguous source match")
    # Decode the entry that actually contains the partitioning loop, not just forwarding overloads.
    return sorted({(g["source"],g["cls"],g["method"]) for g in partition})


def discover(tree, cli, contracts):
    candidates, aliases = inventory(tree)
    expected = contracts["aggregates"]
    evidence = {}
    selected = {}
    for candidate in candidates:
        name = candidate["name"]
        expr = f'"{name}"(v ORDER BY i)'
        sql = (f"WITH input(k,i,v) AS (VALUES {contracts['values']}) "
               f"SELECT cast({expr} AS VARCHAR) AS v, typeof({expr}) AS t FROM input GROUP BY k ORDER BY k")
        proc = subprocess.run([cli, "-batch", "-json", "-init", "/dev/null", ":memory:", "-c", sql],
                              capture_output=True, text=True, timeout=10)
        if proc.returncode:
            evidence[name] = {"status":"incompatible with probe",
                              "error":proc.stderr.splitlines()[0].partition(" Error:")[0]}
            continue
        rows = json.loads(proc.stdout)
        result = {"type":rows[0]["t"], "values":[r["v"] for r in rows]}
        matches = [role for role,wanted in expected.items() if wanted == result]
        evidence[name] = {"result":result, "matched":matches}
        for role in matches:
            if role in selected:
                raise RuntimeError(f"Ambiguous behavior for {role}: {selected[role]['name']} and {name}")
            selected[role] = candidate.copy()
    if set(selected) != set(expected):
        raise RuntimeError(f"Unmatched behaviors: {sorted(set(expected)-set(selected))}")
    groups = locate(tree,selected,contracts["group_method_tokens"])
    return selected,groups,{"tested":evidence,"aliases":aliases}
