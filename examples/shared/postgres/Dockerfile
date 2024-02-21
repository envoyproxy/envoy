FROM postgres:latest@sha256:4d1580c68015bf5dbeb4a2739ba8a3cff2641edd38089c518bbc2f5ccb900867
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
