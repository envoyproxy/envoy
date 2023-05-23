FROM postgres:latest@sha256:b115fe7743919fff0b96f19405ed350017ef9333b3760fc0cc885713eb07f565
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
