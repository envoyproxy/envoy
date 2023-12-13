FROM postgres:latest@sha256:a2282ad0db623c27f03bab803975c9e3942a24e974f07142d5d69b6b8eaaf9e2
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
