#!/bin/bash

set -e

TEST_DATA=test/common/runtime/test_data

# Regular runtime tests.
cd "${TEST_SRCDIR}/envoy"
rm -rf "${TEST_TMPDIR}/${TEST_DATA}"
mkdir -p "${TEST_TMPDIR}/${TEST_DATA}"
cp -RfL "${TEST_DATA}"/* "${TEST_TMPDIR}/${TEST_DATA}"
chmod -R u+rwX "${TEST_TMPDIR}/${TEST_DATA}"

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
  win_test_root="$(echo $TEST_TMPDIR/$TEST_DATA | tr '/' '\\')"
  cmd.exe /C "mklink /D ${win_test_root}\\current ${win_test_root}\\root"
  cmd.exe /C "mklink /D ${win_test_root}\\root\\envoy\\badlink ${win_test_root}\\root\\envoy\\subdir"
  win_loop_path="$(echo $LOOP_PATH | tr '/' '\\')"
  cmd.exe /C "mklink /D ${win_loop_path}\\loop ${win_loop_path}"
fi
