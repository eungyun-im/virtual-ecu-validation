# Requirement change log

| Date | Requirement | Change | Reason |
|---|---|---|---|
| 2026-11-08 | all | First baseline, values fixed from the project plan examples | |
| 2026-11-08 | REQ-06 | "counter not previous + 1 -> discard" became: delta 0 discarded, 1..3 accepted, larger jump discarded and resynchronised | DEF-001: the strict rule turned one corrupted frame into SAFE_STOP. Same idea as MaxDeltaCounter in AUTOSAR E2E. |
