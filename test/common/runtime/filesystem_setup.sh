#!/bin/bash

set -e

cd ${TEST_RUNDIR}
cp -rfL --parents test/common/runtime/test_data ${TEST_TMPDIR}
chmod -R u+rwX ${TEST_TMPDIR}/test/common/runtime/test_data
ln -sf ${TEST_TMPDIR}/test/common/runtime/test_data/root ${TEST_TMPDIR}/test/common/runtime/test_data/current
ln -sf ${TEST_TMPDIR}/test/common/runtime/test_data/root/envoy/subdir ${TEST_TMPDIR}/test/common/runtime/test_data/root/envoy/badlink
