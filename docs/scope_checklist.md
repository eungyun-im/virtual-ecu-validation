# Scope checklist

The project plan's nine work packages, each item with its evidence. Deviations from the plan are marked **Deviation** with the reason.

## A. Requirements and traceability
- [x] 20 requirements in EARS form (function, communication, diagnostics, security, update) — `requirements/requirements.yaml`
- [x] RTM requirement ↔ test ↔ defect, both directions, generated and checked in CI — `tools/rtm.py`, `docs/rtm.md`

## B. Communication (DBC)
- [x] 6 messages: STEER_CMD 10 ms, SPEED_CMD 10 ms, ECU_STATUS 20 ms, UDS 0x7E0 / 0x7DF / 0x7E8 — `dbc/vecu.dbc`
- [x] Factor, offset, range, unit per signal; alive counter and CRC signals; cycle times as attributes
- [x] Diagnostic request and response IDs

## C. Virtual ECU (C)
- [x] SocketCAN send/receive (CI runs on `vcan0`), decoding by the DBC layout — `ecu/app/bus.c`, `ecu/src/ecu.c`
- [x] Range and rate-limit checks — REQ-01..03
- [x] Communication timeout supervision — REQ-04, REQ-05
- [x] E2E (alive counter, CRC8) — REQ-06, REQ-07
- [x] State machine NORMAL / DEGRADED / SAFE_STOP with recovery conditions — REQ-08..11
- [x] DTC storage and status byte — REQ-13
- [x] UDS server 0x10, 0x22, 0x19, 0x14, 0x27, 0x3E (+0x11), NRCs, P2 — REQ-14..18. **Deviation:** ISO-TP is implemented in user space (`ecu/src/isotp.c`) instead of the Linux kernel ISO-TP socket, so the same server runs on the UDP virtual bus where `can-isotp.ko` is not available; the tester side uses `can-isotp` over python-can.
- [x] (optional) Software update 0x34/0x36/0x37 with version and hash checks and rollback on interruption — REQ-20

## D. Rest-bus and fault injector (Python)
- [x] DBC-based periodic transmission (cantools, python-can) — `tools/vecu/restbus.py`
- [x] Faults: missing frames, delay, jitter, counter skip/jump/repeat, CRC errors, out-of-range values, transmission stop and bus-off emulation, replayed and forged frames

## E. Tests (pytest)
- [x] Requirement-based tests, at least one per requirement, REQ ID in the test name and marker
- [x] Boundary tests with `parametrize` tables on both sides of each limit
- [x] State transitions: every allowed transition is exercised; a fixture fails any test that shows a forbidden one
- [x] Fault injection with measured detection time, safe stop and recovery
- [x] Diagnostics: positive responses, NRCs, P2 timing
- [x] Security: replay and forgery rejected, 0x27 attempt limit and delay, UDS request fuzzing on the ASan build
- [x] Fixtures reset the ECU for every test (new process); `smoke`, `regression`, `slow` markers

## F. Static and dynamic analysis
- [x] cppcheck with the MISRA addon, gcc `-fanalyzer`, plus the Clang analyzer — `tools/static_analysis.py`
- [x] MISRA C:2012: 186 findings on v1.0.0 → 0 open; 179 fixed in code, the rest covered by recorded deviations (DEV-01..03) or a confirmed tool false positive (FP-01). CI fails on any new finding or unrecorded suppression — `docs/misra_deviations.md`
- [x] ASan + UBSan on all unit and system tests, libFuzzer — `Makefile`, `tools/fuzz.sh`
- [x] gcov statement and branch coverage, gcovr and lcov HTML — `tools/coverage.sh`

## G. CI (GitHub Actions)
- [x] vcan on the CI runner checked: it loads on `ubuntu-24.04`; the bus layer falls back to the UDP virtual bus elsewhere
- [x] Stages: build → static analysis → ASan build → tests → coverage gate (plus fuzzing and the report)
- [x] Reports kept as artifacts per commit
- [ ] Merge blocking — **Deviation:** GitHub allows branch protection on a private repository only with a paid plan (API answer: 403). `tools/protect_main.sh` makes all six checks required as soon as the repository is public. The demo PR #2 shows the checks failing on a reintroduced defect.
- [x] Git tag baseline per release — `v1.0.0`

## H. Log analyzer (Python)
- [x] Per-message period statistics (mean, min, max, p99, stdev)
- [x] Gaps and timeouts, signal range violations, E2E errors, state timeline
- [x] Summary report with tables and figures — `tools/vecu/log_analyzer.py`, `examples/log_report/`

## I. Defect management and documents
- [x] Defects with steps, environment, expected and actual result, log — `defects/DEF-001.md`. **Deviation:** no JIRA instance; `tools/jira_export.py` writes a CSV for JIRA's importer and the defect is mirrored to GitHub Issues (#1).
- [x] Test Report with verdict and basis on the first line, coverage and analysis results — `tools/test_report.py`, `docs/test_report.md`
- [x] ASPICE mapping SWE.1–SWE.6 (+SUP.8, SUP.9) — `docs/aspice_mapping.md`
- [x] English README, demo video — **Deviation:** the video is 1:40, not 3 minutes; it shows every required scene (fault injection, detection, safe stop, recovery, CI failure).

## Completion criteria
- [x] Every requirement is linked to at least one test in the RTM (20/20)
- [x] CI runs all tests automatically; a failure fails the run (merge blocking: see G)
- [x] The first line of the Test Report shows the verdict and its basis
- [x] The demo shows fault injection → detection → safe stop → recovery
