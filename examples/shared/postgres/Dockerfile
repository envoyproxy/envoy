FROM postgres:latest@sha256:7c0ee16b6a3b4403957ece2c186ff05c57097a557403ae5216ef1286e47c249c
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
