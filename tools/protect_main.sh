#!/bin/bash
# Require the CI gates before anything merges into main (pull request + all checks green).
# GitHub allows branch protection on private repositories only with a paid plan; run this once the
# repository is public (or on GitHub Pro/Team).
set -e
REPO=${1:-eungyun-im/virtual-ecu-validation}
gh api -X PUT "repos/$REPO/branches/main/protection" --input - <<JSON
{
  "required_status_checks": {"strict": true, "contexts": [
    "build + static analysis", "unit tests (ASan + UBSan)", "system tests on CAN (ASan build)",
    "coverage gate (branch >= 80 %)", "fuzzing (libFuzzer, 120 s)", "RTM + Test Report"]},
  "enforce_admins": true,
  "required_pull_request_reviews": null,
  "restrictions": null
}
JSON
echo "main is protected: merges need all six checks green"
