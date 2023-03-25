#!/bin/bash

tmp="${TEST_TMPDIR}/test/integration/admin_html/tempfiles"
echo "tmp=${tmp}"

rm -rf "$tmp"
mkdir -p "$tmp"

echo "sourcing utility file"
source "${TEST_SRCDIR}/envoy/test/integration/test_utility.sh"

export ENVOY_BIN="${TEST_SRCDIR}/envoy/test/integration/admin_html/test_server"
check ls -l $ENVOY_BIN

"${ENVOY_BIN}" -c test/integration/admin_html/web_test.yaml \
  --admin-address-path "$tmp/admin.address" &

echo wait_for_admin_returning_admin_address "$tmp/admin.address"
admin_address=$(wait_for_admin_returning_admin_address "$tmp/admin.address")
echo found admin_address $admin_address

# Verifies that a file can be fetched from the admin address, and it matches
# the source file from the repo.
check_file() {
  file="$1"
  check curl "$admin_address/test?file=$file" --output "$tmp/$file.out"
  check check diff "$tmp/$file.out" "${TEST_SRCDIR}/envoy/test/integration/admin_html/$file"
}

# Verify we can serve all the test files we expect to.
check_file active_stats_test.js
check_file web_test.js
check_file web_test.html

# We also want to verify nothing terrible can happen with this server if we
# specify an invalid file or try to escape out of its subdirectory.
get_status_code_for_invalid_file() {
  file="$1"
  curl "$admin_address/test?file=$file" --dump-header "$tmp/$file.headers" --output /dev/null

  # THe headers will have a line like "HTTP/1.1 404 File Not Found" and we just want the "404".
  grep HTTP/ "$tmp/$file.headers" | cut -d\  -f2
}

check [ "$(get_status_code_for_invalid_file no_such_file)" = "404" ]
check [ "$(get_status_code_for_invalid_file ../evil_escape)" = "400" ]

curl -X POST "$admin_address/quitquitquit"

rm -rf "$tmp"
