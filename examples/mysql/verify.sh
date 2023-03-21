#!/bin/bash -e

export NAME=mysql
export PORT_ADMIN="${MYSQL_PORT_ADMIN:-11300}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"


_mysql () {
    local mysql_client
    mysql_client=(docker run --rm --network mysql_default mysql:latest mysql -h proxy -P 1999 -u root)

    "${mysql_client[@]}" "${@}"
}

export -f _mysql

wait_for 40 bash -c "_mysql -e 'SHOW DATABASES;'"

run_log "Create a mysql database"
_mysql -e "CREATE DATABASE test;"
_mysql -e "show databases;" | grep test

run_log "Create a mysql table"
_mysql -e "USE test; CREATE TABLE test ( text VARCHAR(255) ); INSERT INTO test VALUES ('hello, world!');"
_mysql -e "SELECT COUNT(*) from test.test;" | grep 1

run_log "Check mysql egress stats"
responds_with \
    egress_mysql \
    "http://localhost:${PORT_ADMIN}/stats?filter=egress_mysql"

run_log "Check mysql TCP stats"
responds_with \
    mysql_tcp \
    "http://localhost:${PORT_ADMIN}/stats?filter=mysql_tcp"
