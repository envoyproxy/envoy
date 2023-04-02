FROM postgres:latest@sha256:5a90725b3751c2c7ac311c9384dfc1a8f6e41823e341fb1dceed96a11677303a
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
