#!/usr/bin/env python3
"""Test Report. The first line is the verdict and its basis; the rest is the evidence.

Verdict rule (all must hold for PASS):
  every requirement PASS in the RTM; 0 failed unit, system or fuzz tests; branch coverage >= gate;
  static analysis gate passed; no open Critical/Major defect.
Inputs are the files the CI stages write to reports/out/ and build/unit/. A missing input is
reported as "not run" and makes the verdict INCOMPLETE, never PASS.
"""
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rtm  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "reports" / "out"
COV_GATE = 80.0


def load(name):
    p = OUT / name
    return json.loads(p.read_text()) if p.exists() else None


def main():
    m = rtm.build()
    rtm.write(m)
    unit = [t for t in m["tests"] if t["kind"] == "unit"]
    system = [t for t in m["tests"] if t["kind"] == "system"]
    fuzz = load("fuzz.json")
    cov = load("coverage.json")
    static = load("static.json")
    sysjson = load("systest.json") or {}
    open_major = [d for d in m["defects"] if d["status"] not in ("Verified", "Closed") and d["severity"] in ("Critical", "Major")]
    n_req = len(m["requirements"])
    n_pass = sum(r["verdict"] == "PASS" for r in m["requirements"])
    failed = [t for t in m["tests"] if t["outcome"] == "failed"]
    missing = [n for n, v in (("unit tests", unit and unit[0]["outcome"] != "not run"), ("system tests", system),
                              ("coverage", cov), ("static analysis", static), ("fuzzing", fuzz)) if not v]
    checks = {
        "requirements": n_pass == n_req,
        "tests": not failed,
        "coverage": bool(cov) and cov["branch_percent"] >= COV_GATE,
        "static": bool(static) and static["passed"],
        "defects": not open_major,
    }
    if failed or open_major or (static and not static["passed"]) or (cov and not checks["coverage"]):
        verdict = "FAIL"
    elif missing or not checks["requirements"]:
        verdict = "INCOMPLETE"
    else:
        verdict = "PASS"
    basis = (f"{n_pass}/{n_req} requirements verified; {len(failed)} failed of {len(m['tests'])} tests "
             f"({len(unit)} unit, {len(system)} system, {1 if fuzz else 0} fuzz); "
             + (f"branch coverage {cov['branch_percent']}% (gate {COV_GATE:.0f}%); " if cov else "coverage not run; ")
             + ("static analysis gate passed; " if static and static["passed"] else ("static analysis FAILED; " if static else "static analysis not run; "))
             + f"{len(m['defects'])} defects, {len(open_major)} open Critical/Major"
             + (f"; missing: {', '.join(missing)}" if missing else ""))
    try:
        sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
    except OSError:
        sha = "?"
    L = [f"**Verdict: {verdict}** — {basis}.", "",
         "# Test Report: safety monitor virtual ECU", "",
         f"Commit `{sha}`, generated {datetime.now(timezone.utc).strftime('%Y-%m-%d %H:%M UTC')} by `tools/test_report.py`. "
         f"System tests ran on the `{sysjson.get('bus', '?')}` bus with `{Path(sysjson.get('vecu', '?')).parent.name}` build.", "",
         "## Gates", "", "| Gate | Result | Evidence |", "|---|---|---|",
         f"| All requirements verified | {'PASS' if checks['requirements'] else 'FAIL'} | {n_pass}/{n_req}, `docs/rtm.md` |",
         f"| No failed test | {'PASS' if checks['tests'] else 'FAIL'} | {len(m['tests'])} tests, {len(failed)} failed |",
         f"| Branch coverage >= {COV_GATE:.0f}% | {'PASS' if checks['coverage'] else ('not run' if not cov else 'FAIL')} | "
         + (f"lines {cov['line_percent']}%, branches {cov['branch_percent']}%, functions {cov['function_percent']}%" if cov else "-") + " |",
         f"| Static analysis | {'PASS' if checks['static'] else ('not run' if not static else 'FAIL')} | "
         + (", ".join(f"{k}: {v.get('gating', v.get('warnings', v.get('skipped', '-')))}" for k, v in static["tools"].items() if k != "misra") if static else "-") + " |",
         f"| No open Critical/Major defect | {'PASS' if checks['defects'] else 'FAIL'} | {len(m['defects'])} recorded |", ""]
    L += ["## Requirements", "", "| ID | Requirement | Verdict | Unit | System | Defects |", "|---|---|---|---:|---:|---|"]
    for r in m["requirements"]:
        L.append(f"| {r['id']} | {r['title']} | {r['verdict']} | {len(r['unit'])} | {len(r['system'])} | {', '.join(r['defects']) or '-'} |")
    meas = [t for t in sysjson.get("tests", []) if "p2_ms" in t]
    L += ["", "## Measurements", ""]
    if meas:
        p2 = meas[0]["p2_ms"]
        L.append(f"- P2 (REQ-15): max {p2['max']} ms, mean {p2['mean']} ms over {p2['n']} requests; limit 50 ms.")
    if fuzz:
        L.append(f"- Fuzzing (REQ-19): {fuzz['executions']} executions in {fuzz['seconds']} s, {fuzz['coverage_edges']} edges, exit code {fuzz['exit_code']}.")
    if static and "misra" in static["tools"]:
        mi = static["tools"]["misra"]
        devs = ", ".join(f"{k} ({v})" for k, v in mi.get("deviations_by_rule", {}).items()) or "none"
        L.append(f"- MISRA C:2012 (cppcheck addon, gate): {mi.get('total', 0)} open findings; "
                 f"{mi.get('deviation_sites', 0)} suppressed sites, each tied to a record in `docs/misra_deviations.md` "
                 f"(by rule: {devs}); record problems: {len(mi.get('deviation_problems', []))}.")
    if cov:
        L += ["", "### Coverage by file", "", "| File | Lines | Branches |", "|---|---:|---:|"]
        for f in cov["files"]:
            L.append(f"| `{f['filename']}` | {f['line_percent']}% | {f['branch_percent']}% |")
    L += ["", "## Failed tests", ""]
    L += [f"- `{t['id']}`" for t in failed] or ["None."]
    L += ["", "## Defects", "", "| ID | Severity | Status | Title |", "|---|---|---|---|"]
    for d in m["defects"]:
        L.append(f"| {d['id']} | {d['severity']} | {d['status']} | {d['title']} |")
    L += ["", "## Limits", "",
          "- PC simulation (SIL): timing is measured on a Linux host, not on a target ECU; real-time and electrical faults are out of scope.",
          "- E2E is a safety mechanism, not security: a 4-bit counter and CRC8 do not stop a determined attacker (that needs SecOC).",
          "- SecurityAccess uses a demonstration key algorithm."]
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "test_report.md").write_text("\n".join(L) + "\n")
    print(L[0])
    sys.exit(0 if verdict == "PASS" else 1)


if __name__ == "__main__":
    main()
