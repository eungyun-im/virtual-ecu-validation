"""CAN log analyzer: timing statistics, gaps, range violations, E2E errors and the ECU state timeline.

    python -m vecu.log_analyzer logs/demo.log --dbc dbc/vecu.dbc --out reports/out/log_demo

Reads any log python-can can read (candump .log, .asc, .blf). Writes summary.json, report.md and
two figures. Thresholds come from the DBC (cycle time) and from the requirements (ranges, 30 ms).
"""
import argparse
import collections
import json
import statistics
from pathlib import Path

import can
import cantools

from . import e2e

GAP_FACTOR_LIMIT_MS = 30.0  # REQ-04/05 timeout: a command gap of 30 ms or more is a timeout
VALID_RANGE = {"SteerAngleCmd": (-540.0, 540.0), "SpeedCmd": (0.0, 200.0)}  # REQ-01/02
STATE_NAMES = {0: "NORMAL", 1: "DEGRADED", 2: "SAFE_STOP", 3: "INIT"}


def read_log(path):
    return [m for m in can.LogReader(str(path)) if not m.is_error_frame]


def analyze(msgs, db):
    by_id = collections.defaultdict(list)
    for m in msgs:
        by_id[m.arbitration_id].append(m)
    t0 = msgs[0].timestamp if msgs else 0.0
    result = {"duration_s": (msgs[-1].timestamp - t0) if msgs else 0.0, "frames": len(msgs), "messages": {},
              "gaps": [], "range_violations": [], "e2e_errors": [], "states": []}
    for fid, lst in sorted(by_id.items()):
        try:
            msg = db.get_message_by_frame_id(fid)
            name, cycle = msg.name, msg.cycle_time
        except KeyError:
            name, cycle = f"0x{fid:03X}", None
        ts = [m.timestamp for m in lst]
        periods = [(b - a) * 1000.0 for a, b in zip(ts, ts[1:])]
        entry = {"id": f"0x{fid:03X}", "count": len(lst), "cycle_ms": cycle}
        if periods and cycle:
            entry.update({"mean_ms": round(statistics.mean(periods), 3), "min_ms": round(min(periods), 3),
                          "max_ms": round(max(periods), 3), "stdev_ms": round(statistics.pstdev(periods), 3),
                          "p99_ms": round(sorted(periods)[int(0.99 * (len(periods) - 1))], 3)})
            limit = GAP_FACTOR_LIMIT_MS if name in ("STEER_CMD", "SPEED_CMD") else 3.0 * cycle
            for a, b in zip(ts, ts[1:]):
                if (b - a) * 1000.0 >= limit:
                    result["gaps"].append({"message": name, "start_s": round(a - t0, 4), "gap_ms": round((b - a) * 1000.0, 2)})
        result["messages"][name] = entry
        if fid in e2e.DATA_ID:
            last = None
            for m in lst:
                d = bytes(m.data)
                if not e2e.check(e2e.DATA_ID[fid], d):
                    result["e2e_errors"].append({"message": name, "t_s": round(m.timestamp - t0, 4), "kind": "crc"})
                    continue
                c = d[1] & 0x0F
                if last is not None:
                    delta = (c - last) & 0x0F
                    if delta == 0 or delta > 3:
                        result["e2e_errors"].append({"message": name, "t_s": round(m.timestamp - t0, 4),
                                                     "kind": "repeated" if delta == 0 else f"jump {delta}"})
                last = c
        if name in ("STEER_CMD", "SPEED_CMD", "ECU_STATUS"):
            for m in lst:
                try:
                    sig = msg.decode(bytes(m.data), decode_choices=False)
                except Exception:
                    continue
                for s, (lo, hi) in VALID_RANGE.items():
                    if s in sig and not lo <= sig[s] <= hi:
                        result["range_violations"].append({"message": name, "signal": s, "value": sig[s],
                                                           "t_s": round(m.timestamp - t0, 4)})
                if name == "ECU_STATUS":
                    result["states"].append((round(m.timestamp - t0, 4), STATE_NAMES[int(sig["EcuState"])],
                                             float(sig["SteerOut"]), float(sig["SpeedOut"])))
    trans = []
    for (t, s, *_), prev in zip(result["states"][1:], result["states"]):
        if s != prev[1]:
            trans.append({"t_s": t, "from": prev[1], "to": s})
    result["transitions"] = trans
    # consecutive violations of the same signal form one interval (frames less than 50 ms apart)
    groups = []
    for v in result["range_violations"]:
        g = groups[-1] if groups else None
        if g and g["message"] == v["message"] and g["signal"] == v["signal"] and v["t_s"] - g["end_s"] < 0.05:
            g["end_s"] = v["t_s"]; g["frames"] += 1; g["max"] = max(g["max"], v["value"]); g["min"] = min(g["min"], v["value"])
        else:
            groups.append({"message": v["message"], "signal": v["signal"], "start_s": v["t_s"], "end_s": v["t_s"],
                           "frames": 1, "min": v["value"], "max": v["value"]})
    result["range_violation_intervals"] = groups
    return result


def write_report(res, out, log_name):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    out.mkdir(parents=True, exist_ok=True)
    states = res.pop("states")
    (out / "summary.json").write_text(json.dumps(res, indent=1))
    L = [f"# CAN log analysis: {log_name}", "",
         f"{res['frames']} frames over {res['duration_s']:.2f} s. "
         f"{len(res['gaps'])} gaps, {len(res['range_violations'])} out-of-range frames in {len(res['range_violation_intervals'])} intervals, "
         f"{len(res['e2e_errors'])} E2E errors, {len(res['transitions'])} state changes.", "",
         "## Message timing", "", "| Message | ID | Count | Cycle ms | Mean | Min | Max | p99 | Stdev |", "|---|---|---:|---:|---:|---:|---:|---:|---:|"]
    for name, e in res["messages"].items():
        L.append(f"| {name} | {e['id']} | {e['count']} | {e.get('cycle_ms') or '-'} | {e.get('mean_ms', '-')} | "
                 f"{e.get('min_ms', '-')} | {e.get('max_ms', '-')} | {e.get('p99_ms', '-')} | {e.get('stdev_ms', '-')} |")
    for title, rows, cols in (("Gaps (timeouts)", res["gaps"], ["message", "start_s", "gap_ms"]),
                              ("Range violations", res["range_violation_intervals"], ["message", "signal", "start_s", "end_s", "frames", "min", "max"]),
                              ("E2E errors", res["e2e_errors"], ["message", "kind", "t_s"]),
                              ("State changes", res["transitions"], ["t_s", "from", "to"])):
        L += ["", f"## {title}", ""]
        if not rows:
            L.append("None.")
            continue
        L += ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
        for r in rows[:50]:
            L.append("| " + " | ".join(str(r[c]) for c in cols) + " |")
        if len(rows) > 50:
            L.append(f"\n{len(rows) - 50} more rows in summary.json.")
    # figures
    fig, ax = plt.subplots(figsize=(9, 3.2))
    for name, e in res["messages"].items():
        if e.get("cycle_ms"):
            ax.bar(name, e["max_ms"], color="#d4d4d4")
            ax.bar(name, e["mean_ms"], color="#111111")
    ax.axhline(30, color="#d9480f", ls="--", lw=1)
    ax.text(-0.4, 31, "command timeout 30 ms", color="#d9480f", fontsize=9)
    ax.set_ylabel("period ms (black mean, grey max)")
    for s in ("top", "right"):
        ax.spines[s].set_visible(False)
    fig.tight_layout()
    fig.savefig(out / "timing.png", dpi=120)
    plt.close(fig)
    if states:
        t = [s[0] for s in states]
        code = {"NORMAL": 0, "DEGRADED": 1, "SAFE_STOP": 2, "INIT": 3}
        fig, (a1, a2) = plt.subplots(2, 1, figsize=(9, 4.2), sharex=True)
        a1.step(t, [code[s[1]] for s in states], where="post", color="#111111")
        a1.set_yticks([0, 1, 2, 3]); a1.set_yticklabels(["NORMAL", "DEGRADED", "SAFE_STOP", "INIT"])
        a2.plot(t, [s[2] for s in states], color="#111111", label="steer out (deg)")
        a2.plot(t, [s[3] for s in states], color="#d9480f", label="speed out (km/h)")
        a2.legend(frameon=False, fontsize=8); a2.set_xlabel("time (s)")
        for g in res["gaps"]:
            a1.axvspan(g["start_s"], g["start_s"] + g["gap_ms"] / 1000.0, color="#d4d4d4")
        for ax in (a1, a2):
            for s in ("top", "right"):
                ax.spines[s].set_visible(False)
        fig.tight_layout()
        fig.savefig(out / "states.png", dpi=120)
        plt.close(fig)
        L += ["", "## Figures", "", "![timing](timing.png)", "", "![states](states.png)"]
    (out / "report.md").write_text("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("--dbc", default="dbc/vecu.dbc")
    ap.add_argument("--out", default="reports/out/log")
    a = ap.parse_args()
    res = analyze(read_log(a.log), cantools.database.load_file(a.dbc))
    write_report(res, Path(a.out), Path(a.log).name)
    print(f"{Path(a.out) / 'report.md'}: {len(res['gaps'])} gaps, {len(res['range_violations'])} range violations, "
          f"{len(res['e2e_errors'])} E2E errors, {len(res['transitions'])} state changes")


if __name__ == "__main__":
    main()
