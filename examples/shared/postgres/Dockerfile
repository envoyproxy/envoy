FROM postgres:latest@sha256:ff37e66b0a03594086c3734d73e750f13480ca9bf64b53fafea18be4d5afb9ad
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
