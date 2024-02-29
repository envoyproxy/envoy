FROM postgres:latest@sha256:0e564daae78c2eea56ba57c64711bb9c408a512ce73cf81f9c78623354dd6976
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
