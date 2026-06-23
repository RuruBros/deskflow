#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "usage: fast_install_app.sh <source.app> <destination.app> [codesign identity]" >&2
  exit 2
fi

source_app=$1
destination_app=$2
codesign_identity=${3:-}

if [[ -z "${codesign_identity}" ]]; then
  codesign_identity="-"
fi

source_contents="${source_app}/Contents"
destination_contents="${destination_app}/Contents"
source_macos="${source_contents}/MacOS"
destination_macos="${destination_contents}/MacOS"
source_resources="${source_contents}/Resources"
destination_resources="${destination_contents}/Resources"

fail() {
  echo "fast install failed: $*" >&2
  exit 1
}

require_file() {
  [[ -f "$1" ]] || fail "missing required file: $1"
}

require_dir() {
  [[ -d "$1" ]] || fail "missing required directory: $1"
}

require_file "${source_contents}/Info.plist"
require_file "${source_macos}/Deskflow"
require_file "${source_macos}/deskflow-core"
require_dir "${source_resources}"
require_dir "${destination_macos}"
require_dir "${destination_contents}/Frameworks"
require_dir "${destination_contents}/PlugIns"
mkdir -p "${destination_resources}"

if pgrep -f "${destination_app}/Contents/MacOS" >/dev/null; then
  fail "refusing to update a running app: ${destination_app}"
fi

rewrite_dependency() {
  local dependency=$1
  local binary=$2
  local replacement=

  if [[ "${dependency}" =~ \.framework/Versions/A/([^/]+)$ ]]; then
    local framework="${BASH_REMATCH[1]}"
    local deployed_framework="${destination_contents}/Frameworks/${framework}.framework/Versions/A/${framework}"
    [[ -e "${deployed_framework}" ]] ||
      fail "destination app is missing ${framework}.framework; run install-macos-app first"
    replacement="@loader_path/../Frameworks/${framework}.framework/Versions/A/${framework}"
  elif [[ "${dependency}" == /*.dylib ]]; then
    local dylib
    dylib=$(basename "${dependency}")
    local deployed_dylib="${destination_contents}/Frameworks/${dylib}"
    [[ -e "${deployed_dylib}" ]] ||
      fail "destination app is missing ${dylib}; run install-macos-app first"
    replacement="@loader_path/../Frameworks/${dylib}"
  else
    fail "unsupported non-system dependency in ${binary}: ${dependency}"
  fi

  /usr/bin/install_name_tool -change "${dependency}" "${replacement}" "${binary}"
}

rewrite_binary_dependencies() {
  local binary=$1
  local dependency=

  while IFS= read -r dependency; do
    dependency="${dependency#"${dependency%%[![:space:]]*}"}"
    dependency="${dependency%% (*}"

    case "${dependency}" in
      ""|@*|/System/*|/usr/lib/*)
        continue
        ;;
      *)
        rewrite_dependency "${dependency}" "${binary}"
        ;;
    esac
  done < <(/usr/bin/otool -L "${binary}" | tail -n +2)
}

assert_no_external_dependencies() {
  local binary=$1
  local dependency=

  while IFS= read -r dependency; do
    dependency="${dependency#"${dependency%%[![:space:]]*}"}"
    dependency="${dependency%% (*}"

    case "${dependency}" in
      ""|@*|/System/*|/usr/lib/*)
        ;;
      *)
        fail "non-system dependency remains in ${binary}: ${dependency}"
        ;;
    esac
  done < <(/usr/bin/otool -L "${binary}" | tail -n +2)
}

tmp_dir="${destination_app}.fast-install.$$"
cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

rm -rf "${tmp_dir}"
mkdir -p "${tmp_dir}/MacOS" "${tmp_dir}/Resources"

for binary_name in Deskflow deskflow-core; do
  ditto "${source_macos}/${binary_name}" "${tmp_dir}/MacOS/${binary_name}"
  chmod 755 "${tmp_dir}/MacOS/${binary_name}"
  rewrite_binary_dependencies "${tmp_dir}/MacOS/${binary_name}"
  assert_no_external_dependencies "${tmp_dir}/MacOS/${binary_name}"
done

ditto "${source_contents}/Info.plist" "${tmp_dir}/Info.plist"

if [[ -d "${source_resources}" ]]; then
  shopt -s nullglob
  for resource in "${source_resources}"/*; do
    name=$(basename "${resource}")
    ditto "${resource}" "${tmp_dir}/Resources/${name}"
  done
  shopt -u nullglob
fi

ditto "${tmp_dir}/MacOS/Deskflow" "${destination_macos}/Deskflow"
ditto "${tmp_dir}/MacOS/deskflow-core" "${destination_macos}/deskflow-core"
ditto "${tmp_dir}/Info.plist" "${destination_contents}/Info.plist"

if [[ -d "${tmp_dir}/Resources" ]]; then
  shopt -s nullglob
  for resource in "${tmp_dir}/Resources"/*; do
    name=$(basename "${resource}")
    rm -rf "${destination_resources}/${name}"
    ditto "${resource}" "${destination_resources}/${name}"
  done
  shopt -u nullglob
fi

xattr -dr com.apple.quarantine "${destination_app}" 2>/dev/null || true

/usr/bin/codesign --force --sign "${codesign_identity}" "${destination_macos}/deskflow-core"
/usr/bin/codesign --force --sign "${codesign_identity}" "${destination_app}"
/usr/bin/codesign --verify --deep --strict --verbose=1 "${destination_app}"

trap - EXIT
cleanup
