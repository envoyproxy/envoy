FROM postgres:latest@sha256:50a96a21f2992518c2cb4601467cf27c7ac852542d8913c1872fe45cd6449947
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
