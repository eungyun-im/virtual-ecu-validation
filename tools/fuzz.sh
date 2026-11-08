#!/bin/bash
# libFuzzer on the ECU core (ecu/fuzz/fuzz_ecu.c) with ASan + UBSan. Fails on any finding.
#   FUZZ_SECONDS=60 tools/fuzz.sh
set -u
cd "$(dirname "$0")/.."
CLANG=${CLANG:-clang}
SECS=${FUZZ_SECONDS:-60}
OUT=build/fuzz
mkdir -p "$OUT/corpus" "$OUT/artifacts" reports/out
SRC="ecu/src/e2e.c ecu/src/dtc.c ecu/src/sha256.c ecu/src/isotp.c ecu/src/swupdate.c ecu/src/uds.c ecu/src/ecu.c"
$CLANG -g -O1 -std=c99 -Iecu/include -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all \
  ecu/fuzz/fuzz_ecu.c $SRC -o "$OUT/fuzz_ecu" || exit 1
[ -d ecu/fuzz/corpus ] && cp -n ecu/fuzz/corpus/* "$OUT/corpus/" 2>/dev/null
"$OUT/fuzz_ecu" -max_total_time="$SECS" -max_len=4400 -timeout=10 -print_final_stats=1 \
  -artifact_prefix="$OUT/artifacts/" "$OUT/corpus" > "$OUT/fuzz.log" 2>&1
rc=$?
execs=$(grep -m1 stat::number_of_executed_units "$OUT/fuzz.log" | awk '{print $2}')
cov=$(grep -E "^#[0-9]+" "$OUT/fuzz.log" | tail -1 | grep -oE "cov: [0-9]+" | awk '{print $2}')
printf '{"seconds": %s, "executions": %s, "coverage_edges": %s, "exit_code": %s}\n' \
  "$SECS" "${execs:-0}" "${cov:-0}" "$rc" > reports/out/fuzz.json
cat reports/out/fuzz.json
if [ $rc -ne 0 ]; then
  grep -m5 -E "ERROR|runtime error|SUMMARY" "$OUT/fuzz.log"
  exit 1
fi
