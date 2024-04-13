FROM postgres:latest@sha256:990b4b1fbeeb45b0fe50c2e6989f1134686926658bf0e8bb9415562578a37c60
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
