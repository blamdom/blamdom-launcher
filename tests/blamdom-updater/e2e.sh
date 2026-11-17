#!/bin/bash
# End-to-end test of the in-app updater: a real launcher binary, a fake GitHub API on localhost,
# headless X (xvfb). Needs: xvfb-run, xdotool, scrot, python3.
#
#   tests/blamdom-updater/e2e.sh <bundle-dir> [scenario...]
#
# <bundle-dir> is a bundled launcher folder (contains ./blamdom-launcher). Scenarios:
#   ok-digest   update with the asset digest   -> new files appear, relaunch happens
#   ok-sums     update via SHA256SUMS          -> same
#   bad-digest  wrong checksum                 -> install untouched, launcher keeps running
#   uptodate    release == installed version   -> nothing happens
#   look        no update; screenshots of the wizard and main window
set -u
BUNDLE=$(readlink -f "${1:?bundle dir}"); shift
SCEN=("$@"); [ ${#SCEN[@]} -eq 0 ] && SCEN=(ok-digest ok-sums bad-digest uptodate)
E=${E2E_DIR:-$HOME/e2e}; PORT=18999
PASS=0; FAIL=0
ok()  { echo "  PASS: $*"; PASS=$((PASS+1)); }
bad() { echo "  FAIL: $*"; FAIL=$((FAIL+1)); }

make_release() { # $1 = tag, $2 = digest-mode (digest|sums|wrong)
  rm -rf "$E/api" "$E/rel"; mkdir -p "$E/api/dl" "$E/api/repos/blamdom/blamdom-launcher/releases" "$E/rel"
  cp -r "$BUNDLE" "$E/rel/blamdom-launcher-linux-x86_64"
  echo "update-test-marker" > "$E/rel/blamdom-launcher-linux-x86_64/UPDATED.txt"
  tar -czf "$E/api/dl/blamdom-launcher-linux-x86_64.tar.gz" -C "$E/rel" blamdom-launcher-linux-x86_64
  local sha; sha=$(sha256sum "$E/api/dl/blamdom-launcher-linux-x86_64.tar.gz" | cut -d' ' -f1)
  echo "$sha  blamdom-launcher-linux-x86_64.tar.gz" > "$E/api/dl/SHA256SUMS"
  python3 - "$1" "$2" "$sha" "$PORT" "$E" <<'PY'
import json, sys
tag, mode, sha, port, e = sys.argv[1:]
a = {"name": "blamdom-launcher-linux-x86_64.tar.gz", "browser_download_url": f"http://127.0.0.1:{port}/dl/blamdom-launcher-linux-x86_64.tar.gz", "size": 1}
if mode == "digest": a["digest"] = "sha256:" + sha
if mode == "wrong":  a["digest"] = "sha256:" + "0" * 64
assets = [a]
if mode == "sums":
    assets.append({"name": "SHA256SUMS", "browser_download_url": f"http://127.0.0.1:{port}/dl/SHA256SUMS", "size": 1})
rel = {"tag_name": tag, "name": "Blamdom " + tag, "body": "- end-to-end test release", "draft": False, "prerelease": False,
       "html_url": "https://github.com/blamdom/blamdom-launcher/releases/tag/" + tag, "assets": assets}
json.dump(rel, open(f"{e}/api/repos/blamdom/blamdom-launcher/releases/latest", "w"))
PY
}

run_scenario() {
  local s=$1 tag=v0.1.1 mode=digest
  case $s in ok-sums) mode=sums;; bad-digest) mode=wrong;; uptodate) tag=v0.1.0;; esac
  echo "=== scenario: $s (tag $tag, checksum via $mode)"
  pkill -f "http.server $PORT" 2>/dev/null; pkill -f "^$E/inst/blamdom-launcher" 2>/dev/null; sleep 1
  rm -rf "$E/inst" "$E/inst.update-old" "$E/inst.update-new" "$E/data"; mkdir -p "$E/data"
  cp -r "$BUNDLE" "$E/inst"
  make_release "$tag" "$mode"
  (cd "$E/api" && python3 -m http.server $PORT --bind 127.0.0.1 >"$E/api.log" 2>&1 &)
  sleep 1
  export BLAMDOM_UPDATE_API="http://127.0.0.1:$PORT" BLAMDOMLAUNCHER_DATA_DIR="$E/data"
  [ "$s" != "look" ] && [ "$s" != "uptodate" ] && export BLAMDOM_UPDATE_AUTOINSTALL=1 || unset BLAMDOM_UPDATE_AUTOINSTALL
  if [ "$s" = look ]; then
    xvfb-run -a --server-args="-screen 0 1280x800x24" bash -c '
      "'"$E"'/inst/blamdom-launcher" >"'"$E"'/run-look.log" 2>&1 &
      sleep 9; xdotool key Escape; sleep 1.5; xdotool key Escape; sleep 1.5       # dismiss the first-run wizard
      scrot -o "'"$E"'/shot-look-main.png"
      sleep 9                                                                      # updater creates itself after the main window; first check ~5s later -> dialog
      scrot -o "'"$E"'/shot-look-update.png"
      xdotool key Escape; sleep 1.5
      xdotool mousemove 317 23 click 1; sleep 3                                    # Settings
      scrot -o "'"$E"'/shot-look-settings.png"
      xdotool key Escape; sleep 1.5
      xdotool mousemove 420 23 click 1; sleep 1.5                                  # Help menu
      scrot -o "'"$E"'/shot-look-menu.png"
      xdotool key Escape; sleep 1; pkill -f "^""/inst/blamdom-launcher"
    '
    pkill -f "http.server $PORT" 2>/dev/null
    echo "  screenshots in $E: shot-look-{main,settings,menu,update}.png"
    return
  fi
  xvfb-run -a --server-args="-screen 0 1280x800x24" bash -c '
    "'"$E"'/inst/blamdom-launcher" >"'"$E"'/run-'"$s"'.log" 2>&1 &
    sleep 9
    xdotool key Escape; sleep 1.5; xdotool key Escape; sleep 1.5          # dismiss the first-run wizard
    scrot -o "'"$E"'/shot-'"$s"'-main.png" 2>/dev/null
    sleep 22                                                                 # updater fires ~5s after startup; install + relaunch takes a few more
    xdotool key Escape; sleep 2
    scrot -o "'"$E"'/shot-'"$s"'-after.png" 2>/dev/null
    pkill -f "^$E/inst/blamdom-launcher"; sleep 1
  '
  pkill -f "http.server $PORT" 2>/dev/null
  # the log is the source of truth for "came back": a second main-window boot in the same file
  if grep -c "Main window shown" "$E/run-$s.log" 2>/dev/null | grep -qE '^([2-9]|1[0-9]?)$'; then
    echo RUNNING > "$E/running-$s"
  else
    echo STOPPED > "$E/running-$s"
  fi
  case $s in
    ok-digest|ok-sums)
      [ -f "$E/inst/UPDATED.txt" ] && ok "new files are installed (UPDATED.txt present)" || bad "update was not installed"
      [ -x "$E/inst/blamdom-launcher" ] && ok "installed launcher is executable" || bad "launcher missing/not executable"
      [ ! -e "$E/inst.update-old" ] && ok "old version cleaned up after restart" || bad "old folder left behind: $E/inst.update-old"
      grep -q "Update installed; restarting" "$E"/data/logs/*.log 2>/dev/null && ok "log says update installed" || bad "no 'Update installed' line in logs"
      [ "$(cat "$E/running-$s")" = RUNNING ] && ok "launcher was relaunched and is running" || bad "launcher did not come back after update";;
    bad-digest)
      [ ! -f "$E/inst/UPDATED.txt" ] && ok "tampered download was NOT installed" || bad "installed a download with a wrong checksum!"
      [ -x "$E/inst/blamdom-launcher" ] && [ ! -e "$E/inst.update-old" ] && ok "current install untouched" || bad "current install was modified";;
    uptodate)
      [ ! -f "$E/inst/UPDATED.txt" ] && ok "no update when already on the latest version" || bad "updated although already current"
      grep -q "up to date" "$E"/data/logs/*.log 2>/dev/null && ok "log says up to date" || bad "no 'up to date' log line";;
  esac
  [ -s "$E/shot-$s-main.png" ] && echo "  screenshots: $E/shot-$s-main.png $E/shot-$s-after.png"
}

mkdir -p "$E"
for s in "${SCEN[@]}"; do run_scenario "$s"; done
echo; echo "e2e: $PASS passed, $FAIL failed"; [ $FAIL -eq 0 ]
