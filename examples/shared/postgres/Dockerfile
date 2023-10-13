FROM postgres:latest@sha256:b46651665b72cf3b05b84a6f09579ec5a1e6835037c3d5d6a7f6305033f12eb2
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
