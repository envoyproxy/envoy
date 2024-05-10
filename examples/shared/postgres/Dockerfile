FROM postgres:latest@sha256:ba727f758a75cdd503c6b63db66a5fbc22ded0a228952e9d88e601621ad4de64
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
