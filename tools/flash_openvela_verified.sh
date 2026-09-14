#!/usr/bin/env bash
# Verified OpenVela candidate flashing with a bounded, automatic rollback.
set -Eeuo pipefail

readonly VERSION="${VERSION:-2.1-lightweight-rules-r1}"
readonly PORT="${PORT:-/dev/ttyACM0}"
readonly BAUD="${BAUD:-115200}"
readonly ESPTOOL="${ESPTOOL:-/home/max/.local/bin/esptool}"
readonly CANDIDATE="${CANDIDATE:-/home/max/nuttx-v2.1-lightweight-rules.bin}"
readonly CANDIDATE_SHA256="${CANDIDATE_SHA256:-0f14dd9098a8c492709843b91b508446eb63fc9f43bb788408bf0f53219be020}"
readonly ADDRESS="${ADDRESS:-0x2000}"
readonly MAX_ADDRESS=$(( ${MAX_ADDRESS:-0x4bdb3} ))
readonly PROTECTED_MODEL_ADDRESS=$((0x410000))
readonly RECOVERY="${RECOVERY:-/home/max/recovery-range-0x002000-0x134000.bin}"
readonly RECOVERY_SHA256="fdf5de8d371f45c0b335c29794413d6f53acea3fa728a8197a278a6fd0161b0c"
readonly RECOVERY_SIZE=1253376
readonly LOG_DIR="${LOG_DIR:-/home/max/flash-logs}"

execute=false
if [[ "${1:-}" == "--execute" ]]; then
  execute=true
elif [[ $# -ne 0 ]]; then
  echo "Usage: $0 [--execute]" >&2
  exit 2
fi

mkdir -p "$LOG_DIR"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
log="$LOG_DIR/${VERSION}-${timestamp}.log"
write_started=false

rollback() {
  echo "[$VERSION] Candidate verification failed. Restoring stable recovery image." | tee -a "$log"
  "$ESPTOOL" --chip esp32p4 --port "$PORT" --baud "$BAUD" \
    --before default-reset --after no-reset write-flash "$ADDRESS" "$RECOVERY" 2>&1 | tee -a "$log"
  "$ESPTOOL" --chip esp32p4 --port "$PORT" --baud "$BAUD" \
    --before default-reset --after hard-reset verify-flash "$ADDRESS" "$RECOVERY" 2>&1 | tee -a "$log"
  echo "[$VERSION] Stable recovery verification passed." | tee -a "$log"
}

on_error() {
  local code=$?
  if "$write_started"; then
    rollback || true
  fi
  exit "$code"
}
trap on_error ERR

candidate_size="$(stat -c '%s' "$CANDIDATE")"
candidate_sha="$(sha256sum "$CANDIDATE" | awk '{print $1}')"
recovery_size="$(stat -c '%s' "$RECOVERY")"
recovery_sha="$(sha256sum "$RECOVERY" | awk '{print $1}')"
candidate_end=$((0x2000 + candidate_size - 1))

[[ "$candidate_sha" == "$CANDIDATE_SHA256" ]]
[[ "$recovery_size" -eq "$RECOVERY_SIZE" ]]
[[ "$recovery_sha" == "$RECOVERY_SHA256" ]]
[[ "$candidate_end" -eq "$MAX_ADDRESS" ]]
[[ "$candidate_end" -lt "$PROTECTED_MODEL_ADDRESS" ]]
[[ -c "$PORT" ]]
"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud "$BAUD" chip-id 2>&1 | tee -a "$log"

{
  echo "version=$VERSION"
  echo "candidate=$CANDIDATE"
  echo "candidate_sha256=$candidate_sha"
  echo "write_address=$ADDRESS"
  printf 'candidate_end=0x%x\n' "$candidate_end"
  printf 'protected_model_address=0x%x\n' "$PROTECTED_MODEL_ADDRESS"
  echo "recovery=$RECOVERY"
  echo "recovery_sha256=$recovery_sha"
  echo "execute=$execute"
} | tee -a "$log"

if ! "$execute"; then
  echo "Preflight passed. No flash was written. Re-run with --execute to write the candidate." | tee -a "$log"
  exit 0
fi

write_started=true
"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud "$BAUD" \
  --before default-reset --after no-reset write-flash "$ADDRESS" "$CANDIDATE" 2>&1 | tee -a "$log"
"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud "$BAUD" \
  --before default-reset --after hard-reset verify-flash "$ADDRESS" "$CANDIDATE" 2>&1 | tee -a "$log"
write_started=false
echo "[$VERSION] Candidate verification passed. Log: $log" | tee -a "$log"
