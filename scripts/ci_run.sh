#!/usr/bin/env bash
# Mystery Box Synthesizer -- CI helper.
#   scripts/ci_run.sh "<label>" <command...>
# Runs the command, streams its output, and if it fails publishes the relevant
# error lines as a GitHub Actions annotation so failures are readable from the
# run summary / check-run API without downloading logs.
label="$1"; shift
log="$(mktemp)"
set -o pipefail
"$@" 2>&1 | tee "$log"
rc=${PIPESTATUS[0]}
if [ "$rc" -ne 0 ]; then
  body=$( { grep -iE "error|FAILED|fatal|undefined reference|unresolved|cannot|No such file|assert" "$log" | grep -v "warning" | head -45; echo "--- last lines ---"; tail -25 "$log"; } \
          | tr -d '\r' | cut -c1-400 | sed 's/%/%25/g' | awk 'BEGIN{ORS="%0A"} {print}')
  echo "::error title=${label} failed (exit ${rc})::${body}"
fi
exit "$rc"
