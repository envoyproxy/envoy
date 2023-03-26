#!/bin/bash

tmp="${TEST_TMPDIR}/test/integration/admin_html/tempfiles"
export ENVOY_BIN="${TEST_SRCDIR}/envoy/test/integration/admin_html/test_server"
source "${TEST_SRCDIR}/envoy/test/integration/test_utility.sh"

# Verifies that a file can be fetched from the admin address, and it matches
# the source file from the repo.
check_file() {
  file="$1"
  check curl "$admin_address/test?file=$file" --output "$tmp/$file.out"
  check check diff "$tmp/$file.out" "${TEST_SRCDIR}/envoy/test/integration/admin_html/$file"
}

# We also want to verify nothing terrible can happen with this server if we
# specify an invalid file or try to escape out of its subdirectory.
get_status_code_for_invalid_file() {
  file="$1"
  curl "$admin_address/test?file=$file" --dump-header "$tmp/$file.headers" --output /dev/null

  # The headers will have a line like "HTTP/1.1 404 File Not Found" and we just want the "404".
  grep HTTP/ "$tmp/$file.headers" | cut -d\  -f2
}

function run_testsuite() {
  debug="$1"
  rm -rf "$tmp"
  mkdir -p "$tmp"

  echo Starting test_server $debug
  "${ENVOY_BIN}" $debug -c test/integration/admin_html/web_test.yaml \
    --admin-address-path "$tmp/admin.address" -l info >& "$tmp/envoy.log" &

  echo wait_for_admin_returning_admin_address "$tmp/admin.address"
  admin_address=$(wait_for_admin_returning_admin_address "$tmp/admin.address")
  echo found admin_address $admin_address

  # Verify we can serve all the test files we expect to.
  check_file active_stats_test.js
  check_file web_test.js
  check_file web_test.html
    check [ "$(get_status_code_for_invalid_file no_such_file)" = "404" ]
  check [ "$(get_status_code_for_invalid_file ../evil_escape)" = "400" ]

  # Check that whwen we read the admin home page or stats it works, but
  # we don't want to diff as these files are computed, and we don't need
  # to do detailed content anslysis here.
  check curl "$admin_address/" --output "$tmp/admin.out"
  check grep 'font-family: sans-serif' "$tmp/admin.out"
  check curl "$admin_address/stats?format=active-html" --output "$tmp/stats-active.out"
  check grep 'let statusDiv = null;' "$tmp/stats-active.out"

  curl -X POST "$admin_address/quitquitquit"
  wait
}

# Run the test server in normal log. There will be no logs about reading the web
# resources from the file system while serving admin ppges.
run_testsuite ""
check_not egrep "Read [0-9]+ bytes from " "$tmp/envoy.log"

# Now run the test server in debug mode. There will be logs about reading the web
# resources from the file system while serving admin ppges.
run_testsuite "-debug"
check_debug_log() {
  check egrep "Read [0-9]+ bytes from source/server/admin/html/$1" "$tmp/envoy.log"
}
check_debug_log active_stats.js
check_debug_log admin_head_start.html
check_debug_log admin.css

