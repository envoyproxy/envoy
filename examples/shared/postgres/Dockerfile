FROM postgres:latest@sha256:f4b0987cb4ba8bcc2b90aa33ad8b5786669bec4dc633fc93d1418275e3627b34
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
