FROM postgres:latest@sha256:362a63cb1e864195ea2bc29b5066bdb222bc9a4461bfaff2418f63a06e56bce0
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
