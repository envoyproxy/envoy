#!/bin/bash -e

export NAME=postgres
export DELAY=10

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

_psql () {
    local postgres_client
    postgres_client=(docker run -i --rm --network envoymesh -e "PGSSLMODE=disable" postgres:latest psql -U postgres -h envoy -p 1999)
    "${postgres_client[@]}" "${@}"
}

run_log "Create a postgres database"
_psql -c 'CREATE DATABASE test;'
_psql -c '\l' | grep test

run_log "Create a postgres table"
_psql -d test -c 'CREATE TABLE test ( f SERIAL PRIMARY KEY );'

run_log "Insert some data"
_psql -d test -c 'INSERT INTO test VALUES (DEFAULT);'

run_log "Checking inserted data"
_psql -d test -c 'SELECT * FROM test;' | grep -E '1$'

run_log "Updating data"
_psql -d test -c 'UPDATE test SET f = 2 WHERE f = 1;'

run_log "Raise an exception for duplicate key violation"
_psql -d test -c 'INSERT INTO test VALUES (DEFAULT);' 2>&1 | grep -A1 'duplicate key value violates unique constraint'

run_log "Change some more data"
_psql -d test -c 'DELETE FROM test;'
_psql -d test -c 'INSERT INTO test VALUES (DEFAULT);'

run_log "Check postgres egress stats"
responds_with \
    egress_postgres \
    "http://localhost:8001/stats?filter=egress_postgres"

run_log "Check postgres TCP stats"
responds_with \
    postgres_tcp \
    "http://localhost:8001/stats?filter=postgres_tcp"
