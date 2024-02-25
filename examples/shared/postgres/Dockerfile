FROM postgres:latest@sha256:1bd17d36d605b63fd62f03800a932bae292250659ffc417cf8c29836cc353b5f
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
