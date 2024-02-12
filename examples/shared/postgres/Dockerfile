FROM postgres:latest@sha256:09f23e02d76670d3b346a3c00aa33a27cf57aab8341eedfcdaed41459d14f5c4
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
