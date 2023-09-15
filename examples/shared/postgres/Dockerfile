FROM postgres:latest@sha256:d0c68cd506cde10ddd890e77f98339a4f05810ffba99c881061a12b30a0525c9
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
