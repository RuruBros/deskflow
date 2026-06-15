#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Contributors
# SPDX-License-Identifier: MIT

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: install_app.sh <source.app> <destination.app>" >&2
  exit 2
fi

source_app=$1
destination_app=$2

if [[ ! -d "${source_app}/Contents/MacOS" ]]; then
  echo "source app is missing or invalid: ${source_app}" >&2
  exit 1
fi

if pgrep -f "${destination_app}/Contents/MacOS" >/dev/null; then
  echo "refusing to replace a running app: ${destination_app}" >&2
  exit 1
fi

destination_dir=$(dirname "${destination_app}")
if [[ ! -d "${destination_dir}" ]]; then
  echo "destination directory does not exist: ${destination_dir}" >&2
  exit 1
fi

tmp_app="${destination_app}.tmp.$$"
cleanup() {
  rm -rf "${tmp_app}"
}
trap cleanup EXIT

rm -rf "${tmp_app}"
ditto "${source_app}" "${tmp_app}"
xattr -dr com.apple.quarantine "${tmp_app}" 2>/dev/null || true
codesign --verify --deep --strict --verbose=2 "${tmp_app}"

rm -rf "${destination_app}"
mv "${tmp_app}" "${destination_app}"
trap - EXIT

codesign --verify --deep --strict --verbose=2 "${destination_app}"
