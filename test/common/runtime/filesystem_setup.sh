#!/usr/bin/env bash

set -e

TEST_DATA=test/common/runtime/test_data

# TODO(phlax): Cleanup once bzlmod migration is complete
# Detect workspace name - in WORKSPACE it's 'envoy', in bzlmod it could be '_main' or 'envoy~'
for workspace_name in envoy envoy~ _main; do
    if [[ -d "${TEST_SRCDIR}/${workspace_name}" ]]; then
        ENVOY_WORKSPACE="${workspace_name}"
        break
    fi
done
# Fallback to 'envoy' if detection fails
ENVOY_WORKSPACE="${ENVOY_WORKSPACE:-envoy}"

# Regular runtime tests.
cd "${TEST_SRCDIR}/${ENVOY_WORKSPACE}"
rm -rf "${TEST_TMPDIR:?}/${TEST_DATA}"
mkdir -p "${TEST_TMPDIR}/${TEST_DATA}"
cp -RfL "${TEST_DATA}"/* "${TEST_TMPDIR}/${TEST_DATA}"
chmod -R u+rwX "${TEST_TMPDIR}/${TEST_DATA}"
# Verify text value is treated as a binary blob regardless of source line-ending settings
printf "hello\nworld" > "${TEST_TMPDIR}/${TEST_DATA}/root/envoy/file_lf"
printf "hello\r\nworld" > "${TEST_TMPDIR}/${TEST_DATA}/root/envoy/file_crlf"

# Deliberate symlink of doom.
LOOP_PATH="${TEST_TMPDIR}/${TEST_DATA}/loop"
mkdir -p "${LOOP_PATH}"

# the ln in MSYS2 doesn't handle recursive symlinks correctly,
# so use the cmd built in mklink instead on Windows
if [[ -z "${WINDIR}" ]]; then
  ln -sf "${TEST_TMPDIR}/${TEST_DATA}/root" "${TEST_TMPDIR}/${TEST_DATA}/current"
  ln -sf "${TEST_TMPDIR}/${TEST_DATA}/root/envoy/subdir" "${TEST_TMPDIR}/${TEST_DATA}/root/envoy/badlink"
  ln -sf "${LOOP_PATH}" "${LOOP_PATH}"/loop
else
  # see https://github.com/koalaman/shellcheck/issues/861
  # shellcheck disable=SC1003
  win_test_root="$(echo "${TEST_TMPDIR}/${TEST_DATA}" | tr '/' '\\')"
  cmd.exe /C "mklink /D ${win_test_root}\\current ${win_test_root}\\root"
  cmd.exe /C "mklink /D ${win_test_root}\\root\\envoy\\badlink ${win_test_root}\\root\\envoy\\subdir"
  # see https://github.com/koalaman/shellcheck/issues/861
  # shellcheck disable=SC1003
  win_loop_path="$(echo "$LOOP_PATH" | tr '/' '\\')"
  cmd.exe /C "mklink /D ${win_loop_path}\\loop ${win_loop_path}"
fi
