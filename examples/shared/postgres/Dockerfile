FROM postgres:latest@sha256:766ead991f535a92fd7c7aa577b9d71cf088b9dca1726225391812a751d4bf54
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
