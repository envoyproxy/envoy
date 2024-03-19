FROM postgres:latest@sha256:4784d9bcd8e603179d03732c9d2cd145c3c90e42f29847a95b5317fb37ac4765
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
