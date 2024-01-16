FROM postgres:latest@sha256:49c276fa02e3d61bd9b8db81dfb4784fe814f50f778dce5980a03817438293e3
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
