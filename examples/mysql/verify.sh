#!/bin/bash -e

export NAME=mysql
export DELAY=10

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

_mysql () {
    local mysql_client
    mysql_client=(docker run --network envoymesh mysql:5.5 mysql -h proxy -P 1999 -u root)
    "${mysql_client[@]}" "${@}"
}

run_log "Create a mysql database"
_mysql -e "CREATE DATABASE test;"
_mysql -e "show databases;" | grep test

run_log "Create a mysql table"
_mysql -e "USE test; CREATE TABLE test ( text VARCHAR(255) ); INSERT INTO test VALUES ('hello, world!');"
_mysql -e "SELECT COUNT(*) from test.test;" | grep 1

run_log "Check mysql egress stats"
responds_with \
    egress_mysql \
    "http://localhost:8001/stats?filter=egress_mysql"

run_log "Check mysql TCP stats"
responds_with \
    mysql_tcp \
    "http://localhost:8001/stats?filter=mysql_tcp"
