FROM postgres:latest@sha256:1f703fadc8993f74b82a2706712e61208b1f2bc1658f286c5b68970bbf09f0ae
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
