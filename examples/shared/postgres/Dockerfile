FROM postgres:latest@sha256:7963e577a0ac17c9718b5f8cbc1b10da3147e2de97c534e3c3ac6e54108957cb
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
