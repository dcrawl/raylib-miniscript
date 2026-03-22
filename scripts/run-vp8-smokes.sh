#!/usr/bin/env bash

set -u

BIN="${1:-./build/raylib-miniscript}"
if [[ ! -x "$BIN" ]]; then
  echo "[FAIL] runner: executable not found or not executable: $BIN"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

TMP_DIR="$(mktemp -d /tmp/vp8-smokes.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

PASS_COUNT=0
FAIL_COUNT=0
CASE_TIMEOUT_SEC="${CASE_TIMEOUT_SEC:-90}"

run_case() {
  local name="$1"
  local script="$2"
  local log="$TMP_DIR/$name.log"

  echo "[RUN ] $name -> $script"
  /usr/bin/perl -e 'alarm shift @ARGV; exec @ARGV' "$CASE_TIMEOUT_SEC" "$BIN" "$script" >"$log" 2>&1
  local code=$?

  if [[ $code -ne 0 ]]; then
    if [[ $code -eq 142 ]]; then
      echo "[FAIL] $name (timeout after ${CASE_TIMEOUT_SEC}s)"
    else
      echo "[FAIL] $name (exit $code)"
    fi
    /usr/bin/tail -n 30 "$log"
    FAIL_COUNT=$((FAIL_COUNT + 1))
    return
  fi

  case "$name" in
    vp8_smoke)
      if /usr/bin/grep -q "VP8 smoke timeout reached\|VP8 smoke failed:" "$log"; then
        echo "[FAIL] $name (timeout/failure marker found)"
        /usr/bin/grep -n "VP8 smoke timeout reached\|VP8 smoke failed:\|VP8 smoke done:" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
        return
      fi
      if /usr/bin/grep -q "DidVideoFinish fired" "$log" \
        && /usr/bin/grep -Eq "VP8 smoke done: finish=[1-9]" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "video backend=\|DidVideoFinish fired\|VP8 smoke done:" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected finish markers missing)"
        /usr/bin/grep -n "video backend=\|DidVideoFinish fired\|VP8 smoke done:" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    vp8_loop_smoke)
      if /usr/bin/grep -Eq "DidVideoLoop fired count=[1-9]" "$log" \
        && /usr/bin/grep -Eq "Loop smoke done: loops=[1-9]" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "loop smoke backend=\|DidVideoLoop fired count=\|Loop smoke done:" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected loop markers missing)"
        /usr/bin/grep -n "loop smoke backend=\|DidVideoLoop fired count=\|Loop smoke timeout reached\|Loop smoke done:" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    vp8_finish_smoke)
      if /usr/bin/grep -Eq "DidVideoFinish fired count=[1-9]" "$log" \
        && /usr/bin/grep -Eq "Finish smoke done: finishes=[1-9]" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "finish smoke backend=\|DidVideoFinish fired count=\|Finish smoke done:" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected finish markers missing)"
        /usr/bin/grep -n "finish smoke backend=\|DidVideoFinish fired count=\|Finish smoke timeout reached\|Finish smoke done:" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    vp8_error_smoke)
      if /usr/bin/grep -q "error smoke loadErr=" "$log" \
        && /usr/bin/grep -q "Error smoke done" "$log" \
        && ! /usr/bin/grep -q "Error smoke failed:" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "error smoke loadErr=\|error smoke playerErr=\|Error smoke done" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected diagnostics markers missing)"
        /usr/bin/grep -n "error smoke loadErr=\|error smoke playerErr=\|Error smoke failed:\|Error smoke done" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    vp8_audio_meta_smoke)
      if /usr/bin/grep -q "audio meta: hasAudio=" "$log" \
        && /usr/bin/grep -q "Audio meta smoke done" "$log" \
        && ! /usr/bin/grep -q "Audio meta smoke failed:" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "audio meta: hasAudio=\|Audio meta smoke done" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected audio metadata markers missing)"
        /usr/bin/grep -n "audio meta: hasAudio=\|Audio meta smoke failed:\|Audio meta smoke done" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    vp8_audio_packet_index_smoke)
      if /usr/bin/grep -q "audio packets: count=" "$log" \
        && /usr/bin/grep -q "Audio packet index smoke done" "$log" \
        && ! /usr/bin/grep -q "Audio packet index smoke failed:" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "audio packets: count=\|Audio packet index smoke done" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected audio packet index markers missing)"
        /usr/bin/grep -n "audio packets: count=\|Audio packet index smoke failed:\|Audio packet index smoke done" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    vp8_audio_diag_scaffold_smoke)
      if /usr/bin/grep -q "audio diag: packets=" "$log" \
        && /usr/bin/grep -q "audio scaffold: supported=" "$log" \
        && /usr/bin/grep -q "audio decode stub: status=" "$log" \
        && /usr/bin/grep -q "Audio diag/scaffold smoke done" "$log" \
        && ! /usr/bin/grep -q "Audio diag/scaffold smoke failed:" "$log"; then
        echo "[PASS] $name"
        /usr/bin/grep -n "audio diag: packets=\|audio scaffold: supported=\|audio decode stub: status=\|Audio diag/scaffold smoke done" "$log" | /usr/bin/tail -n 10
        PASS_COUNT=$((PASS_COUNT + 1))
      else
        echo "[FAIL] $name (expected audio diagnostics/scaffold markers missing)"
        /usr/bin/grep -n "audio diag: packets=\|audio scaffold: supported=\|audio decode stub: status=\|Audio diag/scaffold smoke failed:\|Audio diag/scaffold smoke done" "$log" || true
        FAIL_COUNT=$((FAIL_COUNT + 1))
      fi
      ;;
    *)
      echo "[FAIL] unknown smoke case: $name"
      FAIL_COUNT=$((FAIL_COUNT + 1))
      ;;
  esac
}

run_case "vp8_smoke" "$ROOT_DIR/assets/vp8_smoke.ms"
run_case "vp8_loop_smoke" "$ROOT_DIR/assets/vp8_loop_smoke.ms"
run_case "vp8_finish_smoke" "$ROOT_DIR/assets/vp8_finish_smoke.ms"
run_case "vp8_error_smoke" "$ROOT_DIR/assets/vp8_error_smoke.ms"
run_case "vp8_audio_meta_smoke" "$ROOT_DIR/assets/vp8_audio_meta_smoke.ms"
run_case "vp8_audio_packet_index_smoke" "$ROOT_DIR/assets/vp8_audio_packet_index_smoke.ms"
run_case "vp8_audio_diag_scaffold_smoke" "$ROOT_DIR/assets/vp8_audio_diag_scaffold_smoke.ms"

echo "[SUMMARY] pass=$PASS_COUNT fail=$FAIL_COUNT"
if [[ $FAIL_COUNT -ne 0 ]]; then
  exit 1
fi

exit 0
