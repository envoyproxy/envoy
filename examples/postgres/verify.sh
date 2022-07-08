#!/bin/bash -e

export NAME=postgres
export DELAY=10
export PORT_ADMIN="${POSTGRES_PORT_ADMIN:-11600}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

_psql () {
    local postgres_client
    postgres_client=(docker run -i --rm --network postgres_default -e "PGSSLMODE=disable" postgres:latest psql -U postgres -h proxy -p 1999)
    "${postgres_client[@]}" "${@}"
}

DBNAME=testdb

run_log "Create a postgres database"
_psql -c "CREATE DATABASE ${DBNAME};"
_psql -c '\l' | grep ${DBNAME}

run_log "Create a postgres table"
_psql -d ${DBNAME} -c 'CREATE TABLE tbl ( f SERIAL PRIMARY KEY );'

run_log "Insert some data"
_psql -d ${DBNAME} -c 'INSERT INTO tbl VALUES (DEFAULT);'

run_log "Checking inserted data"
_psql -d ${DBNAME} -c 'SELECT * FROM tbl;' | grep -E '1$'

run_log "Updating data"
_psql -d ${DBNAME} -c 'UPDATE tbl SET f = 2 WHERE f = 1;'

run_log "Raise an exception for duplicate key violation"
_psql -d ${DBNAME} -c 'INSERT INTO tbl VALUES (DEFAULT);' 2>&1 | grep -A1 'duplicate key value violates unique constraint'

run_log "Change some more data"
_psql -d ${DBNAME} -c 'DELETE FROM tbl;'
_psql -d ${DBNAME} -c 'INSERT INTO tbl VALUES (DEFAULT);'

run_log "Check postgres egress stats"
responds_with \
    egress_postgres \
    "http://localhost:${PORT_ADMIN}/stats?filter=egress_postgres"

run_log "Check postgres TCP stats"
responds_with \
    postgres_tcp \
    "http://localhost:${PORT_ADMIN}/stats?filter=postgres_tcp"
