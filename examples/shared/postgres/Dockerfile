FROM postgres:latest@sha256:9e3b538a581247a22f8e452d1b797373bb3e1bfae6e3273e9c49b637e080c797
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
