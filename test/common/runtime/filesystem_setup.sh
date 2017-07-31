#!/bin/bash

set -e

TEST_DATA=test/common/runtime/test_data

cd ${TEST_RUNDIR}
case `uname` in
    Darwin)
        mkdir -p ${TEST_TMPDIR}/${TEST_DATA}
        cp -RfL ${TEST_DATA}/* ${TEST_TMPDIR}/${TEST_DATA}
        ;;
    *)
        cp -rfL --parents test/common/runtime/test_data ${TEST_TMPDIR};;
esac

chmod -R u+rwX ${TEST_TMPDIR}/${TEST_DATA}
ln -sf ${TEST_TMPDIR}/${TEST_DATA}/root ${TEST_TMPDIR}/${TEST_DATA}/current
ln -sf ${TEST_TMPDIR}/${TEST_DATA}/root/envoy/subdir ${TEST_TMPDIR}/${TEST_DATA}/root/envoy/badlink
