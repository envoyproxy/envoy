#!/bin/bash -e

export NAME=postgres
export DELAY=10

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

_postgres () {
    local postgres_client
    postgres_client='docker run -i --rm --network envoymesh -e PGSSLMODE=disable -e PGPASSWORD=postgres postgres:latest psql -U postgres -h envoy -p 1999'
    ${postgres_client} <<EOF
        ${1}
EOF
}

run_log "Create a postgres database"
_postgres "CREATE DATABASE test;"
_postgres "\l" | grep test

run_log "Create a postgres table"
_postgres <<EOF
\c test
CREATE TABLE test ( f VARCHAR );
INSERT INTO test VALUES ('hello, world!');"
EOF

_postgres <<EOF | grep 1
\c test
SELECT COUNT(*) FROM test;
EOF

run_log "Check postgres egress stats"
responds_with \
    egress_postgres \
    "http://localhost:8001/stats?filter=egress_postgres"

run_log "Check postgres TCP stats"
responds_with \
    postgres_tcp \
    "http://localhost:8001/stats?filter=postgres_tcp"
