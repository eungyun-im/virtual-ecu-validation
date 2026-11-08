# Virtual ECU Validation Pipeline

A safety monitor ECU written in C and validated on one laptop, from requirements to a Test Report, with no HIL rig: CAN with a DBC, E2E protection, a UDS diagnostic server, fault injection, and CI gates that block a change when a requirement breaks.

This is the software-in-the-loop half of my verification work. The hardware-in-the-loop half (an NMPC on an NXP S32K312 board in a MORAI HILS loop) is in [baremetal-nmpc-hils-verification](https://github.com/eungyun-im/baremetal-nmpc-hils-verification). The ECU here plays the same role as the safety controller there.

**Demo (1:40):** [docs/demo/demo.mp4](docs/demo/demo.mp4) — fault injection → detection → SAFE_STOP → recovery, the log analyzer, and CI blocking a pull request that reintroduced DEF-001.

## Results

| Area | Result |
|---|---|
| Requirements | 20 in EARS form, **20/20 verified**, every one linked to tests in the [RTM](docs/rtm.md) |
| Tests | 49 unit tests on a simulated clock, 59 system tests on SocketCAN `vcan0` in CI, libFuzzer 120 s (229k runs), all under ASan and UBSan |
| Coverage | lines 92.7 %, **branches 83.4 %** (gate 80 %) |
| Static analysis | cppcheck 2.13: 0 warnings · gcc 13.3 `-fanalyzer`: 0 · Clang 18 analyzer: 0 · MISRA C:2012 addon: 186 findings reported by rule (15.5 single exit 60, 17.7 unused return value 43), not resolved |
| Fault detection | steering timeout to SAFE_STOP **30 ms** after the last frame (limit 80 ms); P2 max **under 1 ms** (limit 50 ms) |
| Defects | **DEF-001** found by fault injection: one corrupted frame drove the ECU to SAFE_STOP. Fixed, regression tests fail on the old code |

Numbers are from the CI run on 2026-11-08 ([Test Report](docs/test_report.md), [RTM](docs/rtm.md)); CI writes the same report, verdict on its first line, for every commit.

## What the ECU does

```
STEER_CMD 0x100 (10 ms) ─┐                                      ┌─> ECU_STATUS 0x200 (20 ms + on change)
SPEED_CMD 0x101 (10 ms) ─┤  range check -> E2E check -> timeout │
                         ├─> state machine NORMAL / DEGRADED /  ├─> steering and speed outputs
UDS 0x7E0 / 0x7DF ───────┤   SAFE_STOP, rate limit, DTCs        │
                         └─> ISO-TP -> UDS server ──────────────┴─> UDS 0x7E8
```

1. **Range check**: steering outside ±540.0°, speed outside 0–200 km/h is discarded and sets a DTC.
2. **Rate limit**: steering output changes at most 5.0° per 10 ms cycle.
3. **Timeout**: no steering frame for 30 ms → SAFE_STOP; no speed frame for 30 ms → DEGRADED.
4. **E2E**: CRC8 SAE J1850 over Data ID and payload, 4-bit alive counter. 3 discarded frames in a row → DEGRADED, 10 steering frames → SAFE_STOP.
5. **States**: NORMAL → DEGRADED → SAFE_STOP; back to NORMAL after 100 ms (from DEGRADED) or 1 s (from SAFE_STOP) of valid traffic. SAFE_STOP never goes to DEGRADED.
6. **DTCs**: ISO 14229 status byte (testFailed, pendingDTC, confirmedDTC, …), kept across ECUReset.
7. **UDS**: 0x10, 0x11, 0x14, 0x19, 0x22, 0x27, 0x3E with NRCs, P2 50 ms, S3 5 s; SecurityAccess locks for 10 s after 3 wrong keys.
8. **Software update**: 0x34/0x36/0x37 in the programming session; SHA-256 and anti-rollback checks; an interrupted transfer keeps the running version.

All numbers come from [`requirements/requirements.yaml`](requirements/requirements.yaml).

## How it is tested

| Layer | What | Where |
|---|---|---|
| Unit | Core functions on a simulated 1 ms clock, so 30 ms, 1 s and 5 s timing is checked to the millisecond | `ecu/tests/` |
| System | ECU as a separate process on SocketCAN (`vcan0`) or a UDP virtual bus; DBC-based rest-bus; fault injector; UDS tester (udsoncan over ISO-TP) | `tests/`, `tools/vecu/` |
| Fault injection | missing frames, bus-off, delay, jitter, CRC errors, counter skip/jump/repeat, replay, forged frames, out-of-range values | `tools/vecu/restbus.py` |
| Boundary | `pytest.mark.parametrize` tables on both sides of every limit | `tests/test_function.py` |
| State transitions | every allowed transition; a fixture fails any test that produces a forbidden one | `tests/conftest.py` |
| Security | SecurityAccess rules, replayed and forged CAN frames, UDS request fuzzing (up to 4095 bytes) | `tests/test_security.py`, `ecu/fuzz/` |
| Static | cppcheck, MISRA addon, gcc `-fanalyzer`, Clang analyzer | `tools/static_analysis.py` |
| Dynamic | ASan + UBSan on every unit and system test; libFuzzer on the whole frame input | `Makefile`, `tools/fuzz.sh` |
| Coverage | gcov/gcovr (lcov HTML in CI), unit + system tests, branch gate 80 % | `tools/coverage.sh` |
| Logs | Period statistics, gaps, range violations, E2E errors and state timeline from a CAN log | `tools/vecu/log_analyzer.py` |

Every system test names its requirement (`test_req04_...` and `@pytest.mark.req("REQ-04")`), and `smoke` / `regression` / `slow` markers select the depth.

## CI

[`ci.yml`](.github/workflows/ci.yml) runs on every push and pull request:

```
build + static analysis ─┐
unit tests (ASan/UBSan) ─┤
system tests on CAN ─────┼─> RTM + Test Report (first line = verdict)
coverage gate ───────────┤
fuzzing 120 s ───────────┘
```

A failing gate fails the run; [`tools/protect_main.sh`](tools/protect_main.sh) makes all six checks required for merging into `main` (GitHub allows that on a private repository only with a paid plan, so it is applied when the repository is public). The system job loads `vcan` when the runner allows it (it does on GitHub's Ubuntu 24.04 runners) and otherwise uses the UDP virtual bus; the report says which. Tags `v*` mark baselines.

## Defects

[`defects/`](defects) holds each defect with environment, steps, expected and actual result, log, root cause, fix and verification. `tools/jira_export.py` writes `defects/jira_import.csv` for JIRA's CSV importer, and the same records are mirrored to GitHub Issues.

**DEF-001** (critical): the alive counter check accepted only "previous + 1" and never resynchronised. On a real bus the sender keeps counting when a frame is corrupted, so one bad frame made every later frame fail, and the ECU went to SAFE_STOP within 100 ms. Unit tests missed it because their helper did not advance the counter for corrupted frames; the fault-injection system test caught it. The fix follows the AUTOSAR E2E idea of a maximum counter delta, and REQ-06 was updated with a change-log entry.

## Run it

```sh
python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt
make release asan unit                  # build, unit tests
make systest-asan                       # system tests (vcan0 if present, else the virtual bus)
make coverage && make analyze           # gates
FUZZ_SECONDS=60 make fuzz
make report                             # reports/out/test_report.md
PYTHONPATH=tools .venv/bin/python -m vecu.scenario --log logs/demo.log    # demo scenario with a CAN log
PYTHONPATH=tools .venv/bin/python -m vecu.log_analyzer logs/demo.log      # log report (example: examples/log_report/report.md)
```

## Process

[ASPICE mapping](docs/aspice_mapping.md) (SWE.1–SWE.6, SUP.8, SUP.9) · [RTM](docs/rtm.md) · [requirement change log](requirements/CHANGELOG.md)

## Scope and limits

- **SIL, not HIL.** Timing is measured on a Linux host; real-time behaviour on a target and electrical faults are out of scope (covered by the HILS project).
- **E2E is a safety mechanism, not security.** A 4-bit counter and CRC8 stop faults, not a determined attacker; that needs SecOC.
- **SecurityAccess uses a demonstration key algorithm.**
- **No CANoe/CAPL** (license). The same tests are written with python-can; logs export to formats CANoe reads.
- **No Automotive Ethernet / SOME/IP.** CAN only.

AI tools (Claude Code) were used for implementation and analysis.
