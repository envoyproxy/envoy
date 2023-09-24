FROM postgres:latest@sha256:bf0c7de8c8eadc8c86c631999b050e988a21c80530808f011bd864c899763e0f
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
