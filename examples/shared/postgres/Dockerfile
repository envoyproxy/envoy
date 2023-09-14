FROM postgres:latest@sha256:f2d194fecc644f4d71fb52ff191f17e3550247e2511affbecc2514269f84639a
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
