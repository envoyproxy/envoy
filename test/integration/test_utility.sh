# Helper script for bash integration tests, intended to be source'd from the
# _test.sh.
#
# This was borrowed from PageSpeed's system test infrastructure. Original source
# link:
#
# https://github.com/apache/incubator-pagespeed-mod/blob/c7cc4f22c79ada8077be2a16afc376dc8f8bd2da/pagespeed/automatic/system_test_helpers.sh#L383

CURRENT_TEST="NONE"
function start_test() {
  CURRENT_TEST="$@"
  echo "TEST: $CURRENT_TEST"
}

check() {
  echo "     check" "$@" ...
  "$@" || handle_failure
}

BACKGROUND_PID="?"
run_in_background_saving_pid() {
  echo "     backgrounding:" "$@" ...
  "$@" &
  BACKGROUND_PID="$!"
}

# By default, print a message like:
#   failure at line 374
#   FAIL
# and then exit with return value 1.  If we expected this test to fail, log to
# $EXPECTED_FAILURES and return without exiting.
#
# If the shell does not support the 'caller' builtin, skip the line number info.
#
# Assumes it's being called from a failure-reporting function and that the
# actual failure the user is interested in is our caller's caller.  If it
# weren't for this, fail and handle_failure could be the same.
handle_failure() {
  if [ $# -eq 1 ]; then
    echo FAILed Input: "$1"
  fi

  # From http://stackoverflow.com/questions/685435/bash-stacktrace
  # to avoid printing 'handle_failure' we start with 1 to skip get_stack caller
  local i
  local stack_size=${#FUNCNAME[@]}
  for (( i=1; i<$stack_size ; i++ )); do
    local func="${FUNCNAME[$i]}"
    [ -z "$func" ] && func=MAIN
    local line_number="${BASH_LINENO[(( i - 1 ))]}"
    local src="${BASH_SOURCE[$i]}"
    [ -z "$src" ] && src=non_file_source
    echo "${src}:${line_number}: $func"
  done

  # Note: we print line number after "failed input" so that it doesn't get
  # knocked out of the terminal buffer.
  if type caller > /dev/null 2>&1 ; then
    # "caller 1" is our caller's caller.
    echo "     failure at line $(caller 1 | sed 's/ .*//')" 1>&2
  fi
  echo "in '$CURRENT_TEST'"
  echo FAIL.
  exit 1
}

# The heapchecker outputs some data to stderr on every execution.  This gets intermingled
# with the output from --hot-restart-version, so disable the heap-checker for these runs.
disableHeapCheck () {
  SAVED_HEAPCHECK=${HEAPCHECK}
  unset HEAPCHECK
}

enableHeapCheck () {
  HEAPCHECK=${SAVED_HEAPCHECK}
}

# Scrapes a stat value from an an admin port.
scrape_stat() {
  local ADMIN_ADDRESS="$1"
  local STAT_NAME="$2"
  curl -sg "$ADMIN_ADDRESS"/stats | grep "^${STAT_NAME}: " | cut -f2 -d" "
}

milliseconds() {
  local nanos=$(date +%N | sed 's/^0*//')
  local seconds=$(date +%s)
  echo $((1000*seconds + nanos/1000000))
}

wait_for_stat() {
  local ADMIN_ADDRESS="$1"
  local STAT_NAME="$2"
  local OP="$3"
  local VALUE="$4"
  local TIMEOUT_SEC="$5"
  local start_time_ms=$(milliseconds)
  local end_time=$((SECONDS + TIMEOUT_SEC))
  local ret=""
  while [ "$ret" = "" ]; do
    local stat=$(scrape_stat "$ADMIN_ADDRESS" "$STAT_NAME")
    if [ $stat $OP $VALUE ]; then
      local end_time_ms=$(milliseconds)
      ret="success: $STAT_NAME reached $stat after $((end_time_ms - start_time_ms)) ms"
    elif [ "$SECONDS" -gt "$end_time" ]; then
      ret="timeout: waiting $TIMEOUT_SEC seconds for $STAT_NAME=$stat to reach $VALUE"
    else
      sleep 0.1
    fi
  done
  echo "$ret"
}

[[ -z "${ENVOY_BIN}" ]] && ENVOY_BIN="${TEST_SRCDIR}/envoy/source/exe/envoy-static"
