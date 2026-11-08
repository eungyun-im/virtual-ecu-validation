**Verdict: PASS** — 20/20 requirements verified; 0 failed of 109 tests (49 unit, 59 system, 1 fuzz); branch coverage 83.4% (gate 80%); static analysis gate passed; 1 defects, 0 open Critical/Major.

# Test Report: safety monitor virtual ECU

Commit `b92d738`, generated 2026-11-08 09:27 UTC by `tools/test_report.py`. System tests ran on the `socketcan` bus with `asan` build.

## Gates

| Gate | Result | Evidence |
|---|---|---|
| All requirements verified | PASS | 20/20, `docs/rtm.md` |
| No failed test | PASS | 109 tests, 0 failed |
| Branch coverage >= 80% | PASS | lines 92.7%, branches 83.4%, functions 98.8% |
| Static analysis | PASS | cppcheck: 0, gcc_analyzer: 0, clang_analyzer: 0 |
| No open Critical/Major defect | PASS | 1 recorded |

## Requirements

| ID | Requirement | Verdict | Unit | System | Defects |
|---|---|---|---:|---:|---|
| REQ-01 | Steering command range | PASS | 2 | 8 | - |
| REQ-02 | Speed command range | PASS | 1 | 4 | - |
| REQ-03 | Steering rate limit | PASS | 2 | 1 | - |
| REQ-04 | Steering command timeout | PASS | 2 | 5 | - |
| REQ-05 | Speed command timeout | PASS | 1 | 1 | - |
| REQ-06 | E2E alive counter | PASS | 4 | 5 | DEF-001 |
| REQ-07 | E2E CRC | PASS | 1 | 4 | - |
| REQ-08 | Escalation on invalid frames | PASS | 2 | 3 | DEF-001 |
| REQ-09 | SAFE_STOP behaviour | PASS | 2 | 1 | - |
| REQ-10 | Recovery from SAFE_STOP | PASS | 2 | 1 | - |
| REQ-11 | Recovery from DEGRADED and forbidden transitions | PASS | 1 | 2 | - |
| REQ-12 | Status message | PASS | 2 | 2 | - |
| REQ-13 | DTC status byte | PASS | 2 | 1 | - |
| REQ-14 | Supported UDS services | PASS | 5 | 10 | - |
| REQ-15 | P2 response time | PASS | 2 | 1 | - |
| REQ-16 | SecurityAccess session | PASS | 1 | 1 | - |
| REQ-17 | SecurityAccess attempt limit | PASS | 2 | 2 | - |
| REQ-18 | Session timeout | PASS | 2 | 2 | - |
| REQ-19 | Robustness of the diagnostic server | PASS | 0 | 1 | - |
| REQ-20 | Software update with integrity check and rollback | PASS | 8 | 4 | - |

## Measurements

- P2 (REQ-15): max 0.057 ms, mean 0.031 ms over 140 requests; limit 50 ms.
- Fuzzing (REQ-19): 247422 executions in 120 s, 302 edges, exit code 0.
- MISRA C:2012 (cppcheck addon, reported only): 186 findings; most frequent rules 15.5 (60), 17.7 (43), 12.3 (14), 12.1 (10), 10.8 (9), 14.4 (7).

### Coverage by file

| File | Lines | Branches |
|---|---:|---:|
| `ecu/app/bus.c` | 56.1% | 31.5% |
| `ecu/app/main.c` | 92.7% | 79.2% |
| `ecu/src/dtc.c` | 100.0% | 100.0% |
| `ecu/src/e2e.c` | 100.0% | 100.0% |
| `ecu/src/ecu.c` | 99.0% | 93.3% |
| `ecu/src/isotp.c` | 90.4% | 78.2% |
| `ecu/src/sha256.c` | 100.0% | 100.0% |
| `ecu/src/swupdate.c` | 100.0% | 88.2% |
| `ecu/src/uds.c` | 98.6% | 89.7% |

## Failed tests

None.

## Defects

| ID | Severity | Status | Title |
|---|---|---|---|
| DEF-001 | Critical | Verified | One corrupted command frame drives the ECU to SAFE_STOP (alive counter never resynchronises) |

## Limits

- PC simulation (SIL): timing is measured on a Linux host, not on a target ECU; real-time and electrical faults are out of scope.
- E2E is a safety mechanism, not security: a 4-bit counter and CRC8 do not stop a determined attacker (that needs SecOC).
- SecurityAccess uses a demonstration key algorithm.
