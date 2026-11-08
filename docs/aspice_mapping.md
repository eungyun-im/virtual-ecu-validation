# Automotive SPICE mapping (SWE.1 to SWE.6, plus SUP.8 and SUP.9)

Where each work product of this project sits in Automotive SPICE (process names from v3.1, v4.0 names in brackets where they changed).

> No assessment was done and no capability level is claimed. This is a one-person portfolio project, so independent review, agreed plans and change approval by another person are missing. The table shows what exists and what does not.

| Process | Expected outcome (summary) | Work products in this repository | Gaps |
|---|---|---|---|
| **SWE.1** Software requirements analysis | Requirements that are specific, verifiable and traceable, with their changes managed | `requirements/requirements.yaml`: 20 requirements in EARS form, each with a numeric pass criterion; `requirements/CHANGELOG.md` (REQ-06 changed after DEF-001) | No stakeholder review; system requirements are implicit in the project plan |
| **SWE.2** Software architectural design | Components, interfaces, dynamic behaviour, resource budgets | `dbc/vecu.dbc` (CAN interface, cycle times, E2E Data IDs); `ecu/include/vecu/*.h` (module interfaces); state machine and timing in README; bus access layer `ecu/app/bus.h` | No separate architecture document with diagrams for every module |
| **SWE.3** Detailed design and unit construction | Units designed and coded to the design | C99 units in `ecu/src/` (e2e, monitor in ecu.c, dtc, isotp, uds, swupdate, sha256); strict warnings `-Wall -Wextra -Wconversion -Werror` | Design lives in code comments |
| **SWE.4** Software unit verification | Strategy, criteria, static verification, unit tests, results, traceability | `ecu/tests/` (49 unit tests on a simulated clock, ASan/UBSan); `tools/static_analysis.py` (cppcheck, gcc -fanalyzer, clang analyzer gates; MISRA reported); coverage gate (branch >= 80 %); libFuzzer | MISRA findings are reported, not resolved; no MC/DC |
| **SWE.5** Software integration and integration test [component verification and integration verification] | Integrated software tested against the architecture and interfaces | The ECU runs as a process on a CAN bus (vcan or the UDP virtual bus); `tests/` system tests drive it through the DBC-based rest-bus, fault injector and UDS tester (ISO-TP over CAN) | One ECU only; no other real ECUs on the bus |
| **SWE.6** Software qualification test [software verification] | Tests against the software requirements, results traceable to requirements | Requirement-based system tests (`@pytest.mark.req`), boundary and state-transition tests, fault injection with measured detection times; `docs/rtm.md`; `reports/out/test_report.md` (first line = verdict) | SIL only: target timing and electrical faults are covered by the separate HILS project |
| **SUP.8** Configuration management | Identified items, baselines, reproducible builds | Git; pinned tool versions (`requirements-dev.txt`); tags `v*` as baselines; CI artifacts per commit (Test Report, coverage, logs) | No release notes beyond tags |
| **SUP.9** Problem resolution management | Problems recorded, analysed, tracked to closure | `defects/DEF-*.md` (environment, steps, expected/actual, log, root cause, fix, verification), `defects/jira_import.csv` for JIRA, mirrored to GitHub Issues; each fix has a regression test that fails on the old code | Finder, fixer and verifier are the same person |
| **SUP.1** Quality assurance (partly) | Independent check that the process is followed | CI gates on every push and pull request (`.github/workflows/ci.yml`) | Not independent |

## How the pieces connect

```
requirements.yaml --(REQ id in test names / markers)--> unit + system tests --> results (JUnit, systest.json)
        |                                                        |
        +------------------ tools/rtm.py <-----------------------+----- defects/DEF-*.md
                                 |
                       docs/rtm.md, tools/test_report.py --> verdict on line 1
```
