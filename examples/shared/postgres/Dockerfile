FROM postgres:latest@sha256:8877c953e0ab485eb8d9b42590b900ce49e643ab9e4caee431661893242d60f6
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
