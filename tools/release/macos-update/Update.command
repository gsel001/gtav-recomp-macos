#!/bin/zsh
# Updates executable code only. The launcher syncs the frontend on the next normal launch.
set -euo pipefail
here=${0:A:h}
app=${1:-/Applications/GTAV.app}
fail() { print -u2 -- "$*"; exit 1; }
[[ -f "$app/Contents/Resources/gtav/librexruntime.dylib" && -f "$app/Contents/MacOS/GTAV" ]] || fail "An existing GTA V recomp v0.3.0 app is required. Pass its .app path as the first argument."
[[ -w "$app/Contents/MacOS" && -w "$app/Contents/Resources/gtav" ]] || fail "The app is not writable by this account. Update a copy that you own."
update_app_version=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
[[ "$update_app_version" == "0.3.0" ]] || fail "This update requires an upstream v0.3.0 app (found $update_app_version)."
set +e
/usr/bin/pgrep -x gtav_fast >/dev/null 2>&1
running=$?
set -e
(( running == 1 )) || fail "Close GTA V before updating (or the process check failed)."
for binary in GTAV gtav_fast; do
  /usr/bin/codesign --verify --strict "$here/$binary"
done
backup=${GTAV_UPDATE_BACKUP_DIR:-"$HOME/Library/Application Support/GTAV Recomp Updates/$(date +%Y%m%d-%H%M%S)-$$"}
[[ ! -e "$backup" ]] || fail "Backup path already exists: $backup"
mkdir -p "$backup"
/bin/cp -p "$app/Contents/MacOS/GTAV" "$backup/GTAV"
/bin/cp -p "$app/Contents/Resources/gtav/gtav_fast" "$backup/gtav_fast"
/bin/cp -p "$app/Contents/Info.plist" "$backup/Info.plist"
/usr/bin/ditto "$app/Contents/_CodeSignature" "$backup/_CodeSignature"
trap 'update_exit_code=$?; if (( update_exit_code != 0 )); then /bin/cp -p "$backup/GTAV" "$app/Contents/MacOS/GTAV"; /bin/cp -p "$backup/gtav_fast" "$app/Contents/Resources/gtav/gtav_fast"; /usr/bin/ditto "$backup/_CodeSignature" "$app/Contents/_CodeSignature"; print -u2 "Update failed; previous executables and signature restored."; fi' EXIT
/bin/cp -p "$here/GTAV" "$app/Contents/MacOS/GTAV.codex-new"
/bin/cp -p "$here/gtav_fast" "$app/Contents/Resources/gtav/gtav_fast.codex-new"
/bin/mv -f "$app/Contents/MacOS/GTAV.codex-new" "$app/Contents/MacOS/GTAV"
/bin/mv -f "$app/Contents/Resources/gtav/gtav_fast.codex-new" "$app/Contents/Resources/gtav/gtav_fast"
/usr/bin/codesign --force --sign - "$app"
/usr/bin/codesign --verify --deep --strict "$app"
print -- "Updated $app"
print -- "Previous executables backed up at: $backup"
print -- "Game files, saves, settings and shader caches were not modified. Open GTA V normally to play."
