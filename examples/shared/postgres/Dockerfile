FROM postgres:latest@sha256:2881f973a604ea35d149c129fb98a676945d00ea7cb281ef1ee7ab2d9e22b309
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
