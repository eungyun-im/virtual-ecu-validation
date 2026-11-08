#!/usr/bin/env python3
"""Static analysis gate.

  cppcheck (warning, performance, portability, style)   gate: 0 findings of severity error/warning
  cppcheck MISRA C:2012 addon                           reported by rule, not a gate (no rule texts)
  gcc -fanalyzer (GCC >= 10)                            gate: 0 warnings
  clang --analyze (Clang static analyzer)               gate: 0 warnings
Results: reports/out/static.json and reports/out/static_*.txt. A tool that is not installed is
reported as "skipped" and does not pass silently: the JSON says so and the Test Report shows it.
"""
import collections
import json
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "reports" / "out"
SRC = sorted(str(p.relative_to(ROOT)) for p in (ROOT / "ecu" / "src").glob("*.c")) + \
      sorted(str(p.relative_to(ROOT)) for p in (ROOT / "ecu" / "app").glob("*.c"))
INC = ["-Iecu/include"]


def run(cmd):
    return subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)


def tool_version(exe, arg="--version"):
    r = run([exe, arg])
    return (r.stdout or r.stderr).strip().splitlines()[0] if (r.stdout or r.stderr) else "?"


def cppcheck(exe, misra):
    xml_path = OUT / ("cppcheck_misra.xml" if misra else "cppcheck.xml")
    cmd = [exe, "--std=c99", "--enable=warning,style,performance,portability", "--inline-suppr",
           "--suppress=missingIncludeSystem", "--xml", "--xml-version=2", "-D__linux__", "-D_GNU_SOURCE", *INC, *SRC]
    if misra:
        cmd.insert(1, "--addon=misra")
    r = run(cmd)
    xml_path.write_text(r.stderr)
    findings = []
    try:
        root = ET.fromstring(r.stderr)
    except ET.ParseError:
        return None
    for e in root.iter("error"):
        loc = e.find("location")
        if e.get("id") in ("missingInclude", "checkersReport", "unmatchedSuppression"):
            continue
        findings.append({"id": e.get("id"), "severity": e.get("severity"), "msg": e.get("msg"),
                         "file": loc.get("file") if loc is not None else "", "line": int(loc.get("line")) if loc is not None else 0})
    return findings


def gcc_analyzer():
    gcc = shutil.which("gcc")
    ver = run([gcc, "-dumpfullversion"]).stdout.strip() if gcc else ""
    if not gcc or int(ver.split(".")[0]) < 10:
        return None, f"gcc {ver or 'missing'} has no -fanalyzer"
    warnings = []
    for f in SRC:
        r = run([gcc, "-std=c99", "-D_GNU_SOURCE", "-fanalyzer", "-c", "-o", "/dev/null", *INC, f])
        warnings += [l for l in r.stderr.splitlines() if "warning:" in l]
    (OUT / "static_gcc_analyzer.txt").write_text("\n".join(warnings) + "\n")
    return warnings, f"gcc {ver}"


def clang_analyzer():
    clang = shutil.which("clang")
    if not clang:
        return None, "clang missing"
    warnings = []
    for f in SRC:
        r = run([clang, "--analyze", "-std=c99", "-D_GNU_SOURCE", "-Xanalyzer", "-analyzer-output=text", "-o", "/dev/null", *INC, f])
        warnings += [l for l in r.stderr.splitlines() if "warning:" in l]
    (OUT / "static_clang_analyzer.txt").write_text("\n".join(warnings) + "\n")
    return warnings, tool_version(clang)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    result = {"files": SRC, "tools": {}, "gate": {}}
    exe = shutil.which("cppcheck")
    if exe:
        ver = tool_version(exe)
        base = cppcheck(exe, misra=False)
        misra = cppcheck(exe, misra=True)
        misra_rules = collections.Counter(f["id"].replace("misra-c2012-", "") for f in (misra or []) if f["id"].startswith("misra"))
        gating = [f for f in (base or []) if f["severity"] in ("error", "warning")]
        result["tools"]["cppcheck"] = {"version": ver, "findings": base, "gating": len(gating),
                                       "style": sum(1 for f in (base or []) if f["severity"] not in ("error", "warning"))}
        result["tools"]["misra"] = {"version": ver, "total": sum(misra_rules.values()), "by_rule": dict(misra_rules.most_common()),
                                    "available": misra is not None}
        result["gate"]["cppcheck"] = len(gating) == 0
        (OUT / "static_misra.txt").write_text("".join(
            f"{f['file']}:{f['line']}: {f['id']} {f['msg']}\n" for f in (misra or []) if f["id"].startswith("misra")))
    else:
        result["tools"]["cppcheck"] = {"skipped": "cppcheck not installed"}
    w, info = gcc_analyzer()
    result["tools"]["gcc_analyzer"] = {"version": info, "warnings": len(w)} if w is not None else {"skipped": info}
    if w is not None:
        result["gate"]["gcc_analyzer"] = len(w) == 0
    w, info = clang_analyzer()
    result["tools"]["clang_analyzer"] = {"version": info, "warnings": len(w)} if w is not None else {"skipped": info}
    if w is not None:
        result["gate"]["clang_analyzer"] = len(w) == 0
    result["passed"] = all(result["gate"].values()) and bool(result["gate"])
    (OUT / "static.json").write_text(json.dumps(result, indent=1))
    for name, t in result["tools"].items():
        print(f"{name:15s} {json.dumps({k: v for k, v in t.items() if k not in ('findings', 'by_rule')})}")
    if "cppcheck" in result["tools"] and result["tools"]["cppcheck"].get("findings"):
        for f in result["tools"]["cppcheck"]["findings"][:20]:
            print(f"  {f['severity']:8s} {f['id']:28s} {f['file']}:{f['line']} {f['msg']}")
    print("static analysis gate:", "PASS" if result["passed"] else "FAIL")
    sys.exit(0 if result["passed"] else 1)


if __name__ == "__main__":
    main()
