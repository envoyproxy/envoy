FROM postgres:latest@sha256:e2135391c55eb2ecabaaaeef4a9538bb8915c1980953fb6ce41a2d6d3e4b5695
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
