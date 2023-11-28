FROM postgres:latest@sha256:3d9ed832906091d609cfd6f283e79492ace01ba15866b21d8a262e8fd1cdfb55
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
