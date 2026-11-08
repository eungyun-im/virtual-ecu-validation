#!/bin/bash
# Statement and branch coverage of ecu/src and ecu/app from the unit tests and the system tests
# (both built with --coverage by `make coverage`). Gate: branch coverage >= COV_BRANCH_MIN (80 %).
# Writes reports/out/coverage.json, a gcovr HTML report, and an lcov HTML report when lcov exists.
set -u
cd "$(dirname "$0")/.."
MIN=${COV_BRANCH_MIN:-80}
GCOVR=${GCOVR:-$( [ -x .venv/bin/gcovr ] && echo .venv/bin/gcovr || echo gcovr)}
mkdir -p reports/out/coverage
PYTHONNOUSERSITE=1 $GCOVR -r . build/cov --filter 'ecu/src/' --filter 'ecu/app/' --exclude-unreachable-branches \
  --json-summary reports/out/coverage.json --html-details reports/out/coverage/index.html --txt-summary \
  > reports/out/coverage.txt || exit 1
cat reports/out/coverage.txt | tail -4
if command -v lcov >/dev/null 2>&1; then
  lcov --quiet --capture --directory build/cov --rc lcov_branch_coverage=1 --output-file reports/out/lcov.info \
    && lcov --quiet --extract reports/out/lcov.info "$PWD/ecu/*" --rc lcov_branch_coverage=1 -o reports/out/lcov.info \
    && genhtml --quiet --branch-coverage reports/out/lcov.info -o reports/out/lcov-html
fi
python3 - "$MIN" <<'EOF'
import json, sys
s = json.load(open("reports/out/coverage.json"))
mn = float(sys.argv[1])
print(f"coverage: lines {s['line_percent']}%, branches {s['branch_percent']}% (gate >= {mn}%)")
sys.exit(0 if s["branch_percent"] >= mn else 1)
EOF
