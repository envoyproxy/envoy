FROM postgres:latest@sha256:8775adb39f0db45cf4cdb3601380312ee5e9c4f53af0f89b7dc5cd4c9a78e4e8
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
