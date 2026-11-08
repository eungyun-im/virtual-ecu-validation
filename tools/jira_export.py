#!/usr/bin/env python3
"""Export defects/DEF-*.md to a CSV that JIRA's CSV importer reads (System > External System Import > CSV).
Columns map to Summary, Issue Type, Priority, Severity (custom label), Status, Component, Labels,
Affects Version, Fix Version, Environment and Description (steps, expected, actual, log in wiki markup)."""
import csv
import glob
import re
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]


def section(body, name):
    m = re.search(rf"^## {name}\n(.*?)(?=^## |\Z)", body, re.S | re.M)
    return m.group(1).strip() if m else ""


def to_wiki(md):
    return md.replace("```", "{noformat}")


rows = []
for p in sorted(glob.glob(str(ROOT / "defects" / "DEF-*.md"))):
    _, meta, body = Path(p).read_text().split("---", 2)
    m = yaml.safe_load(meta)
    desc = "\n\n".join(f"h3. {n}\n{to_wiki(section(body, n))}" for n in
                       ("Steps to reproduce", "Expected result", "Actual result", "Root cause", "Fix", "Verification"))
    rows.append({"Summary": f"[{m['id']}] {m['title']}", "Issue Type": m["type"], "Priority": m["priority"],
                 "Labels": " ".join([f"severity-{m['severity'].lower()}", *[r.lower() for r in m["requirements"]],
                                     m["found_by"].replace(" ", "-")]),
                 "Status": m["status"], "Component": m["component"].split()[0], "Affects Version": m["found_in"],
                 "Fix Version": m["fixed_in"], "Environment": to_wiki(section(body, "Environment")), "Description": desc})
out = ROOT / "defects" / "jira_import.csv"
with open(out, "w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
    w.writeheader()
    w.writerows(rows)
print(f"{len(rows)} issues -> {out}")
