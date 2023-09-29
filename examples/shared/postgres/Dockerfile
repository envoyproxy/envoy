FROM postgres:latest@sha256:379b7a1223b394106cc20d18a5177ed77738003416057e8898cde10e6b7a082a
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
