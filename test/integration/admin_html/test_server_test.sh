#!/usr/bin/env bash

tmp="${TEST_TMPDIR}/test/integration/admin_html/tempfiles"
export ENVOY_BIN="${TEST_SRCDIR}/envoy/test/integration/admin_html/test_server"
# shellcheck source=test/integration/test_utility.sh
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

# Verifies that an html-generated admin endpoint contains some expected fragments.
verify_admin_contains() {
  check curl "$admin_address$1" --output "$tmp/admin.out"
  check grep "$2" "$tmp/admin.out"
}

# Starts up test_server (a variant of envoy-static) and runs a few queries
# against its admin port to ensure it can serve test files, and that we are
# able to read html/css/js files from the file-system when run with debug.
run_testsuite() {
  rm -rf "$tmp"
  mkdir -p "$tmp"

  debug="$1"
  echo Starting test_server "$debug"
  if [ "$debug" = "debug" ]; then
    "${ENVOY_BIN}" debug \
      -c test/integration/admin_html/web_test.yaml \
      --admin-address-path "$tmp/admin.address" -l info >& "$tmp/envoy.log" &
  else
    "${ENVOY_BIN}" \
      -c test/integration/admin_html/web_test.yaml \
      --admin-address-path "$tmp/admin.address" -l info >& "$tmp/envoy.log" &
  fi

  echo wait_for_admin_returning_admin_address "$tmp/admin.address"
  admin_address=$(wait_for_admin_returning_admin_address "$tmp/admin.address")
  echo found admin_address "$admin_address"

  # Verify we can serve all the test files we expect to.
  check_file active_stats_test.js
  check_file web_test.js
  check_file web_test.html
    check [ "$(get_status_code_for_invalid_file no_such_file)" = "404" ]
  check [ "$(get_status_code_for_invalid_file ../evil_escape)" = "400" ]

  # Check that whwen we read the admin home page or stats it works, but
  # we don't want to diff as these files are computed, and we don't need
  # to do detailed content anslysis here.
  verify_admin_contains "/" "font-family: sans-serif"
  verify_admin_contains "/stats?format=active-html" "let statusDiv = null;"

  curl -X POST "$admin_address/quitquitquit"
  wait
}

# Run the test server in normal log. There will be no logs about reading the web
# resources from the file system while serving admin ppges.
run_testsuite ""

# Check that we are not reading the resources from the file-system when
# test_server was not run with debug mode.
check_not egrep "Read [0-9]+ bytes from " "$tmp/envoy.log"

# Now run the test server in debug mode. There will be logs about reading the web
# resources from the file system while serving admin ppges.
run_testsuite "debug"

# Verify that the envoy log writen by run_testsuite contains the specified
# fragment. This is used to ensure we are reading the resources from the file
# system in debug mode.
check_debug_log() {
  check egrep "Read [0-9]+ bytes from source/server/admin/html/$1" "$tmp/envoy.log"
}
check_debug_log active_stats.js
check_debug_log admin_head_start.html
check_debug_log admin.css
check_debug_log histograms.js

