FROM postgres:latest@sha256:075d8c46b50d0d72c3a519766a3a91ca184253309d2e03e944e3fe6299814262
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
