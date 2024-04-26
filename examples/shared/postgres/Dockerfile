FROM postgres:latest@sha256:c3ae2edaab802159e3c3528c51002441ed2e0ddc7ac58f66628f74eca2869535
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
