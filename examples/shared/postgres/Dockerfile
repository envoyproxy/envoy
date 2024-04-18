FROM postgres:latest@sha256:5c587078ef86f4b5fd24c9ed4394429e59035ead421873c14135d5b57c117908
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
