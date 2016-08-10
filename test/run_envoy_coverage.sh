#!/bin/bash

BINARY_DIR=$1
SOURCE_DIR=$2
PROJECT_SOURCE_DIR=$3
PROJECT_BINARY_DIR=$4
GCOVR=$5
GCOVR_EXTRA_ARGS=$6

find $BINARY_DIR -name *.gcda -exec rm {} \;
$PROJECT_SOURCE_DIR/test/run_envoy_tests.sh $PROJECT_SOURCE_DIR $PROJECT_BINARY_DIR
$GCOVR $GCOVR_EXTRA_ARGS -r $SOURCE_DIR --html --html-details -o $BINARY_DIR/coverage.html --exclude-unreachable-branches --print-summary > coverage_summary.txt

coverage_value=`grep -Po 'lines: \K(\d|\.)*' coverage_summary.txt`
coverage_threshold=95.0
coverage_failed=`echo "$coverage_value<$coverage_threshold" | bc`
if test $coverage_failed -eq 1; then
    echo Code coverage $coverage_value is lower than limit of $coverage_threshold
    exit 1
else
    echo Code coverage $coverage_value is good and higher than limit of $coverage_threshold
fi
