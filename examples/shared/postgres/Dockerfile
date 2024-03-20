FROM postgres:latest@sha256:6b841c8f6a819884207402f1209a8116844365df15fca8cf556fc54a24c70800
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
