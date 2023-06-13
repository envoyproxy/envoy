FROM postgres:latest@sha256:2642aecef7a61b5eafd45138c5b380845db1be3ab9dfaaa5133660341bc203e8
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
