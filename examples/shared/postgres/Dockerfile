FROM postgres:latest@sha256:1e90f8560705b0daccbd8eb25573627c8452fc9282496433aab1259ae4c85824
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
