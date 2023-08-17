FROM postgres:latest@sha256:86817d5b3096e71d26bb5328e5f5d9d184c02bc3e511b252ed6cffaae9e3f745
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
