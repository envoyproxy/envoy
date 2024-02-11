FROM postgres:latest@sha256:4d1b17af6f66b852ee3a721f6691a2ca7352f9d28f570a6a48cee4ebe646b2fd
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
