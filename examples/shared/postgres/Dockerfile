FROM postgres:latest@sha256:8d45935fb783e72c871072e9eb72ee8c817a9eaf25c405b0e62526b14191368d
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
