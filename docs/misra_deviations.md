# MISRA C:2012 compliance

**Result: 0 open findings.** The cppcheck MISRA addon reported 186 findings on v1.0.0. All of them were fixed in the code except for two records: one deviation the tool reports, DEV-01 (4 sites), and one class of confirmed tool false positives, FP-01 (5 sites). A second deviation, DEV-02 (2 sites), was found by review because the tool does not report it. CI fails the build on any new finding, on a suppression that names no record here, and on a suppression that no longer matches a finding (`tools/static_analysis.py`, gate `misra`).

## Scope and tool

| Item | Value |
|---|---|
| Code | `ecu/src/` (ECU core: monitoring, E2E, state machine, DTCs, ISO-TP, UDS, software update) and `ecu/app/` (host adapter: SocketCAN or UDP sockets, clock, signals, stderr) |
| Standard | MISRA C:2012 with Amendments 1 and 2, C99 |
| Tool | cppcheck 2.13 `--addon=misra --library=posix`, run in CI on every commit |
| Rule texts | Not supplied (they are licensed), so findings carry rule numbers only |

Limits of the evidence:
- cppcheck's addon is not a qualified MISRA checker and does not cover every rule. For example, it does not report rule 11.3 on casts between pointers to two different struct types (DEV-02).
- Rules marked "undecidable" are covered only as far as the addon implements them, plus the dynamic evidence: ASan/UBSan on all tests, and fuzzing.
- A real project would add a qualified tool such as Polyspace, Helix QAC or Parasoft. A real project would also approve deviations formally. This project has a single author, so each record states its rationale but has no independent approval.

## Findings fixed (v1.0.0 → this version)

| Rule | Category | Before | Fix |
|---|---|---:|---|
| 15.5 single point of exit | Advisory | 60 | Every function now has one `return`. Each UDS service builds an NRC code and responds once at the end; the larger services are split into helpers (`security_seed`, `security_key`, `req_download`, `transfer_data`, `transfer_exit`, `put_dtc`). |
| 17.7 return value used | Required | 43 | Results of `memcpy`, `memset`, `snprintf`, `fprintf`, `close`, `clock_gettime` and `sigaction` are cast to `void` where the result carries no information. |
| 12.3 comma operator | Advisory | 14 | One declaration per line (struct members and locals). |
| 12.1 precedence | Advisory | 10 | Explicit parentheses, e.g. `p[(4u * i) + 1u]`, `(~e) & g`. |
| 10.8 composite cast | Required | 9 | Wrap-around time comparisons no longer cast `(int32_t)(now - deadline)`. The new `vecu_time_reached()` compares the unsigned difference against 2^31. Pointer-difference casts are replaced by index arithmetic. |
| 14.4 boolean controlling expression | Required | 7 | Flags that were `int` (`safe`, `degraded`, `is_steer`, `in_range`, `rate_limited`, `suppress`, `do_trace`) are now `bool`. |
| 10.3 assignment to narrower or different type | Required | 6 | Same change: boolean results go into `bool` variables. |
| 8.9 object at block scope | Advisory | 4 | Lookup tables used by a single function (DTC codes, SHA-256 constants, VIN, serial number) moved into that function as `static const`. |
| 2.5 unused macro | Advisory | 4 | Removed. The documentation they carried (signal scaling, bit 7 of the DTC status) is kept as comments. |
| 17.8 parameter modified | Advisory | 3 | The CRC routine works on a local copy of its `init` parameter. |
| 10.4 operands of different essential type | Required | 3 | Session subfunction compared against `(uint8_t)UDS_SESSION_*`. |
| 17.3 implicit function declaration | Mandatory | 3 | Tool false positives: the calls use macros from system headers that cppcheck does not read. After the socket code was restructured, 2 sites remain (FP-01). (`atoi` was also replaced by a checked `strtoul`.) |
| 8.7 external linkage used in one unit | Advisory | 3 | The incremental SHA-256 functions are now `static`. Only the one-shot `vecu_sha256()` is exported. |
| 11.3 cast between object pointer types | Required | 2 | The vbus peer address is stored as `struct sockaddr_in`, not as a byte array cast to `struct sockaddr *`. The remaining casts the socket API needs are DEV-02. |
| 18.4 pointer arithmetic | Advisory | 2 | `host:port` parsing uses array indices. |
| 21.6 / 21.5 / 21.10 standard I/O, signals, time | Required | 4 | DEV-01: host adapter only. |
| 15.7 `else if` without `else` | Required | 1 | Final `else` added. |
| 10.7, 10.6 composite operands | Required | 2 | Shift count computed into a local, and the `?:` operands carry the target type. Five more 10.6 sites surfaced after the first refactoring and were fixed by using `bool` and `if`/`else`. |
| 13.3 increment with other side effects | Advisory | 1 | `argv[++i]` replaced by explicit indexing in `parse_args()`. |
| 21.14 `memcmp` on strings | Required | 1 | The image magic is a `uint8_t` array, not a string literal. |
| misra-config | (tool) | 4 | FP-01. |

Behaviour is unchanged. The 49 unit tests, 59 system tests and the fuzzer pass on the refactored code, and branch coverage stays above the 80 % gate.

## Records

Each suppression in the code names one of these IDs (`/* cppcheck-suppress misra-c2012-<rule> ; DEV-nn */`). The gate checks that the ID exists and lists that rule.

| ID | Rules | Scope | Reason | Risk and mitigation |
|---|---|---|---|---|
| DEV-01 | 21.5, 21.6, 21.10 | `ecu/app/main.c`, `ecu/app/bus.c` (`<signal.h>`, `<stdio.h>`, `<time.h>`) | The host adapter runs the core as a Linux process. It needs SIGINT/SIGTERM for a clean stop (so coverage data is written), `clock_gettime` for the 1 ms tick, and stderr for diagnostics. On a target, the CAN driver and a timer interrupt replace this file. | None of these headers is included by `ecu/src/`; the build would show it. Signal handling is limited to setting a `volatile sig_atomic_t`. Output goes only to stderr and never affects control. |
| DEV-02 | 11.3 | `ecu/app/bus.c`: `sa_of_can()`, `sa_of_in()` | The POSIX socket API (`bind`, `sendto`) takes `const struct sockaddr *`. Passing `struct sockaddr_can` or `struct sockaddr_in` needs this cast by design of the API. | Both casts are in two one-line functions, from a correctly typed and aligned object to the type the kernel expects. cppcheck 2.13 does not report them; the record comes from review and needs no suppression. |
| DEV-03 | 8.7 | `ecu/src/uds.c`: `vecu_uds_key()` | The SecurityAccess key function is part of the ECU's interface to the test environment: the unit tests compute the expected key with it. cppcheck analyses only the ECU sources, so it sees a single caller. | The function is pure (no state, no side effects). A production ECU would not expose its key algorithm; the README states that the algorithm is a demonstration. |
| FP-01 | 17.3, config | `ecu/app/bus.c`: `ioctl(…, SIOCGIFINDEX, …)`, `socket(AF_INET, …)`, `CAN_ERR_FLAG \| CAN_RTR_FLAG` | Tool false positive. cppcheck does not parse system headers, so the macros from `<sys/ioctl.h>`, `<sys/socket.h>` and `<linux/can.h>` are unknown to it ("misra-config"), and the calls that use them look undeclared (17.3). | Every function is declared by its system header. The build runs with `-std=c99 -Wall -Werror`, so an implicit declaration would fail compilation. |

## How to check

```sh
python3 tools/static_analysis.py          # needs cppcheck >= 2.13; writes reports/out/static_misra.txt
```

`static_misra.txt` lists any open finding, then every deviation site with its record ID. `static.json` gives the counts by rule.
