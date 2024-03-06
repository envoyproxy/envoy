FROM postgres:latest@sha256:f58300ac8d393b2e3b09d36ea12d7d24ee9440440e421472a300e929ddb63460
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
