#!/bin/bash -e

export NAME=memcached

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


run_log "Test set"
telnet 127.0.0.1 1999 set foo 0 0 3\r\nFOO\r\n | grep STORED
telnet 127.0.0.1 1999 set bar 0 0 3\r\nBAR\r\n | grep STORED

run_log "Test get"
telnet 127.0.0.1 1999 get foo | grep FOO
telnet 127.0.0.1 1999 get bar | grep BAR

run_log "Test memcached stats"
responds_with \
    egress_memcached \
    "http://localhost:8001/stats?usedonly&filter=redis.egress_memcached.command"
