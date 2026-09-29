#!/usr/bin/env python3
"""Check a pcrecomp disasm32 catalog: prints one line, exits 1 if below --min functions."""
import argparse, json, sys

ap = argparse.ArgumentParser()
ap.add_argument("catalog")
ap.add_argument("--min", type=int, default=1)
a = ap.parse_args()
with open(a.catalog) as f:
    doc = json.load(f)
funcs = doc["functions"] if isinstance(doc, dict) else doc
st = doc.get("stats", {}) if isinstance(doc, dict) else {}
n = len(funcs)
print(f"functions={n} coverage={st.get('coverage_pct', '?')}% min={a.min} -> {'OK' if n >= a.min else 'FAIL'}")
sys.exit(0 if n >= a.min else 1)
