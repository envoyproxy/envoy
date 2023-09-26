FROM postgres:latest@sha256:f86808f55807714e1cf8bc9d60b3f3230132d31ebadd4284c3b47e38d980bdcc
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
