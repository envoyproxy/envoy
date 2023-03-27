FROM postgres:latest@sha256:f9bad88192bb345322392bd98d5036187be738246f27e13d2c6faad615f4eaac
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
