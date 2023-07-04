FROM postgres:latest@sha256:a8da151019fbccb64b199aa77cb15caafd3903e943a66753f46865766062ed72
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
