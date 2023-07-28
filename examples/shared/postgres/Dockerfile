FROM postgres:latest@sha256:08349aace3a7e83792d8fb317ef3a545081de29bcb7a70e79fcd2342a041ab16
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
