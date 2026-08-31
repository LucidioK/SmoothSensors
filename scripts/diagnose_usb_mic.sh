#!/usr/bin/env bash
# Diagnoses (and, with --fix, attempts to repair) USB host / microphone enumeration
# problems on the UNO Q's Linux side.
#
# This must run ON THE BOARD, not on the PC — it inspects kernel/sysfs USB state
# (dwc3 role, vbus regulator, driver binding, dmesg) that isn't visible remotely.
# The board's single USB-C port is dual-role: it shows up as a *device* to a PC
# (that's the enumeration you're already seeing work), but needs to be switched
# into *host* role for a hub/mic to enumerate on the board's own `lsusb`. This
# script checks and, if needed, forces that switch, then walks the rest of the
# chain down to an actual test recording.
#
# Usage (run on the board):
#   ./diagnose_usb_mic.sh          # diagnose only, print findings
#   ./diagnose_usb_mic.sh --fix    # also attempt safe, reversible remediations
#
# From your PC, run it on the board without copying it over first:
#   ssh <board-user@host> 'bash -s' -- --fix < scripts/diagnose_usb_mic.sh

set -uo pipefail  # no -e: we want to keep going through failed checks and report all of them

FIX=0
[[ "${1:-}" == "--fix" ]] && FIX=1

PASS=0
WARN=0
FAIL=0
FIXED=0

section() { printf '\n== %s ==\n' "$1"; }
ok()      { printf '  [OK]   %s\n' "$1"; PASS=$((PASS+1)); }
warn()    { printf '  [WARN] %s\n' "$1"; WARN=$((WARN+1)); }
fail()    { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
fixed()   { printf '  [FIX]  %s\n' "$1"; FIXED=$((FIXED+1)); }
info()    { printf '  [..]   %s\n' "$1"; }

as_root() {
  if [[ "$(id -u)" -eq 0 ]]; then
    "$@"
  elif sudo -n true 2>/dev/null; then
    sudo "$@"
  else
    return 127
  fi
}

section "Environment"
info "date: $(date)"
info "uname: $(uname -a)"
[[ -r /proc/device-tree/model ]] && info "board model: $(tr -d '\0' < /proc/device-tree/model)"
info "user: $(whoami) (groups: $(id -Gn))"
if [[ "$(id -u)" -eq 0 ]]; then
  ok "running as root — fixes (if requested) can run directly"
elif sudo -n true 2>/dev/null; then
  ok "passwordless sudo available — fixes (if requested) can run via sudo"
else
  warn "not root and no passwordless sudo — remediation steps that need root will be skipped"
fi
[[ $FIX -eq 1 ]] && info "--fix passed: will attempt remediations" || info "diagnostic-only run (pass --fix to attempt repairs)"

section "USB enumeration snapshot"
if command -v lsusb >/dev/null; then
  LSUSB_BEFORE="$(lsusb 2>&1)"
  echo "$LSUSB_BEFORE" | sed 's/^/  /'
  N_DEVICES=$(echo "$LSUSB_BEFORE" | grep -vc 'Linux Foundation.*root hub')
  if [[ $N_DEVICES -gt 0 ]]; then
    ok "lsusb shows $N_DEVICES non-root-hub device(s)"
  else
    fail "lsusb shows nothing but root hubs — hub/mic is not enumerating"
  fi
else
  fail "lsusb not installed (apt install usbutils)"
fi
command -v lsusb >/dev/null && { info "topology (lsusb -t):"; lsusb -t 2>&1 | sed 's/^/  /'; }

section "dwc3 USB controller role (host vs device vs otg)"
DWC3_MODE_FILES=()
if [[ -d /sys/kernel/debug ]]; then
  if ! mount | grep -q 'on /sys/kernel/debug type debugfs'; then
    if [[ $FIX -eq 1 ]]; then
      if as_root mount -t debugfs none /sys/kernel/debug 2>/dev/null; then
        fixed "mounted debugfs at /sys/kernel/debug"
      else
        warn "debugfs not mounted and couldn't mount it (need root) — dwc3 mode can't be inspected"
      fi
    else
      warn "debugfs not mounted at /sys/kernel/debug — pass --fix (as root) to mount it, or run: sudo mount -t debugfs none /sys/kernel/debug"
    fi
  fi
fi
if [[ -d /sys/kernel/debug/usb ]]; then
  while IFS= read -r f; do DWC3_MODE_FILES+=("$f"); done < <(find /sys/kernel/debug/usb -maxdepth 1 -name 'mode' 2>/dev/null)
fi
if [[ ${#DWC3_MODE_FILES[@]} -eq 0 ]]; then
  warn "no dwc3 debugfs 'mode' file found under /sys/kernel/debug/usb/*/mode (controller name may differ, or debugfs unavailable)"
else
  for f in "${DWC3_MODE_FILES[@]}"; do
    MODE="$(cat "$f" 2>/dev/null || echo unreadable)"
    if [[ "$MODE" == *host* ]]; then
      ok "$f = $MODE"
    else
      fail "$f = $MODE (expected 'host' for a hub/mic to enumerate)"
      if [[ $FIX -eq 1 ]]; then
        if echo host | as_root tee "$f" >/dev/null 2>&1; then
          fixed "wrote 'host' to $f"
        else
          warn "couldn't write 'host' to $f (need root, or controller doesn't support forcing this way)"
        fi
      else
        info "pass --fix to try: echo host | sudo tee $f"
      fi
    fi
  done
fi

section "USB Type-C data/power role"
if [[ -d /sys/class/typec ]] && [[ -n "$(ls -A /sys/class/typec 2>/dev/null)" ]]; then
  for port in /sys/class/typec/*/; do
    port_name="$(basename "$port")"
    [[ "$port_name" == *-partner ]] && continue
    for attr in data_role power_role; do
      [[ -r "${port}${attr}" ]] && info "$port_name $attr = $(cat "${port}${attr}")"
    done
  done
else
  info "no /sys/class/typec ports exposed on this board (may not apply to this SoC)"
fi

section "VBUS regulator state"
VBUS_FOUND=0
for reg in /sys/class/regulator/regulator.*; do
  [[ -r "$reg/name" ]] || continue
  name="$(cat "$reg/name" 2>/dev/null)"
  [[ "$name" == *vbus* || "$name" == *usb* ]] || continue
  VBUS_FOUND=1
  state="$(cat "$reg/state" 2>/dev/null || echo unknown)"
  if [[ "$state" == "enabled" ]]; then
    ok "$reg ($name) = enabled"
  else
    fail "$reg ($name) = $state"
    if [[ $FIX -eq 1 && -w "$reg/enable" ]]; then
      if echo 1 | as_root tee "$reg/enable" >/dev/null 2>&1; then
        fixed "wrote 1 to $reg/enable"
      else
        warn "couldn't enable $reg (need root, or not software-controllable)"
      fi
    fi
  fi
done
[[ $VBUS_FOUND -eq 0 ]] && info "no regulator with 'vbus'/'usb' in its name found (naming may differ on this board)"

section "Recent kernel USB messages"
if command -v dmesg >/dev/null; then
  DMESG_USB="$(dmesg 2>/dev/null | grep -iE 'usb|xhci|dwc3|vbus|typec|overcurrent|over-current' | tail -30)"
  if [[ -n "$DMESG_USB" ]]; then
    echo "$DMESG_USB" | sed 's/^/  /'
    if echo "$DMESG_USB" | grep -qiE 'overcurrent|over-current'; then
      fail "overcurrent reported — check the hub's own power supply/cable, not just the board"
    fi
    if echo "$DMESG_USB" | grep -qiE 'device descriptor read.*error|device not accepting address'; then
      fail "descriptor read errors — classic sign of a flaky cable/connector or an underpowered hub"
    fi
  else
    warn "dmesg has no recent USB-related lines (or dmesg requires root on this system)"
  fi
else
  warn "dmesg not available"
fi

section "Host controller driver binding"
for drv in dwc3 xhci-hcd; do
  d="/sys/bus/platform/drivers/$drv"
  [[ -d "$d" ]] || d="/sys/bus/usb/drivers/$drv"
  if [[ -d "$d" ]]; then
    bound="$(find "$d" -maxdepth 1 -type l 2>/dev/null | grep -v '/module$' || true)"
    if [[ -n "$bound" ]]; then
      ok "$drv has bound device(s): $(echo "$bound" | xargs -n1 basename | tr '\n' ' ')"
      if [[ $FIX -eq 1 && $FAIL -gt 0 ]]; then
        for dev_link in $bound; do
          dev="$(basename "$dev_link")"
          info "rebinding $drv/$dev (unbind + bind) to force re-enumeration"
          echo "$dev" | as_root tee "$d/unbind" >/dev/null 2>&1
          sleep 1
          if echo "$dev" | as_root tee "$d/bind" >/dev/null 2>&1; then
            fixed "rebound $dev to $drv"
          else
            warn "rebind of $dev to $drv failed (need root)"
          fi
        done
      fi
    else
      warn "$drv driver present but has no bound devices"
    fi
  fi
done

section "udev"
if command -v systemctl >/dev/null && systemctl is-active --quiet systemd-udevd 2>/dev/null; then
  ok "systemd-udevd is active"
else
  warn "systemd-udevd not active or systemctl unavailable"
fi
if [[ $FIX -eq 1 ]] && command -v udevadm >/dev/null; then
  as_root udevadm trigger --action=add --attr-match=subsystem=usb >/dev/null 2>&1 && info "triggered udevadm re-add for usb subsystem"
  as_root udevadm settle --timeout=5 2>/dev/null
fi

if [[ $FIX -eq 1 ]]; then
  section "Re-checking lsusb after fix attempts"
  sleep 2
  LSUSB_AFTER="$(lsusb 2>&1)"
  echo "$LSUSB_AFTER" | sed 's/^/  /'
  N_AFTER=$(echo "$LSUSB_AFTER" | grep -vc 'Linux Foundation.*root hub')
  if [[ $N_AFTER -gt 0 ]]; then
    ok "hub/device now visible after remediation"
  else
    fail "still nothing but root hubs after remediation attempts"
  fi
fi

section "Audio device chain"
if lsusb 2>/dev/null | grep -qi 'audio\|microphone\|mic'; then
  ok "an audio-class USB device is enumerated"
elif [[ $(lsusb 2>/dev/null | grep -vc 'Linux Foundation.*root hub') -eq 0 ]]; then
  warn "skipping audio checks — no non-root-hub USB device enumerated yet"
else
  warn "a USB device is enumerated but doesn't self-describe as audio in its lsusb string (may still work — checking ALSA)"
fi

if command -v arecord >/dev/null; then
  ARECORD_L="$(arecord -l 2>&1)"
  echo "$ARECORD_L" | sed 's/^/  /'
  if echo "$ARECORD_L" | grep -q '^card'; then
    ok "arecord -l lists at least one capture card"
    CARD_LINE="$(echo "$ARECORD_L" | grep '^card' | head -1)"
    CARD_NUM="$(echo "$CARD_LINE" | sed -n 's/^card \([0-9]*\).*/\1/p')"
    DEV_NUM="$(echo "$CARD_LINE" | sed -n 's/.*device \([0-9]*\).*/\1/p')"
    if ! lsmod 2>/dev/null | grep -q snd_usb_audio; then
      warn "snd-usb-audio kernel module not loaded"
      if [[ $FIX -eq 1 ]]; then
        as_root modprobe snd-usb-audio 2>/dev/null && fixed "loaded snd-usb-audio module" || warn "couldn't load snd-usb-audio (need root)"
      fi
    else
      ok "snd-usb-audio module loaded"
    fi
    if fuser /dev/snd/* >/dev/null 2>&1; then
      warn "something already has /dev/snd/* open (likely the running app) — this can make a manual arecord test time out even though the mic is fine; stop the app first: arduino-app-cli app stop ArduinoApps/smoothsensors03"
    fi
    TESTFILE="/tmp/usb_mic_test_$$.wav"
    info "attempting a 1s test capture: arecord -D plughw:CARD=${CARD_NUM},DEV=${DEV_NUM:-0}"
    if timeout 5 arecord -q -D "plughw:CARD=${CARD_NUM},DEV=${DEV_NUM:-0}" -f S16_LE -r 16000 -c 1 -d 1 "$TESTFILE" 2>/tmp/usb_mic_test_err_$$; then
      SIZE=$(stat -c%s "$TESTFILE" 2>/dev/null || echo 0)
      if [[ "$SIZE" -gt 44 ]]; then
        ok "test capture succeeded, wrote $SIZE bytes to $TESTFILE"
      else
        fail "arecord exited 0 but captured file is empty/header-only ($SIZE bytes)"
      fi
    else
      fail "test capture failed: $(cat /tmp/usb_mic_test_err_$$ 2>/dev/null)"
    fi
    rm -f "/tmp/usb_mic_test_err_$$"
  else
    fail "arecord -l lists no capture cards"
  fi
else
  fail "arecord not installed"
fi

if ! id -nG | grep -qw audio; then
  warn "current user ($(whoami)) is not in the 'audio' group — some ALSA setups require this for device permissions"
  if [[ $FIX -eq 1 ]]; then
    as_root usermod -aG audio "$(whoami)" 2>/dev/null && fixed "added $(whoami) to audio group (log out/in, or restart the ssh session, for it to take effect)" || warn "couldn't add to audio group (need root)"
  fi
else
  ok "user is in the 'audio' group"
fi

section "Summary"
printf '  %d OK, %d WARN, %d FAIL' "$PASS" "$WARN" "$FAIL"
[[ $FIX -eq 1 ]] && printf ', %d FIXED' "$FIXED"
printf '\n'

if [[ $FAIL -gt 0 && ( $FIX -eq 0 || $FIXED -eq 0 ) ]]; then
  cat <<'EOF'

Still broken after software checks/fixes? This exact signature — dwc3 stuck off
host role and/or the vbus regulator staying disabled no matter what's tried —
is what killed a previous UNO Q board (traced to ESD damage on the USB PHY).
Before assuming that again on a brand-new board:
  - Try a different USB-A port on the hub, and a different cable, to rule those out.
  - Try the SAME hub+mic on another host (PC) to confirm they still work standalone.
  - Confirm the hub's own power source (PD charger) is actually delivering power —
    a hub with no upstream power will never let devices enumerate regardless of the
    board's role.
  - If none of that helps and this is reproducible cold-boot to cold-boot, it's
    worth treating as a hardware fault and reporting to Arduino rather than
    continuing to fight it in software.
EOF
fi
