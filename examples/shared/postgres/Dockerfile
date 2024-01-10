FROM postgres:latest@sha256:b09f2562ab14fcae750cfc5ae457cd97e90c37679f520bc4a84180913de90261
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
