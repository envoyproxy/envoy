FROM postgres:latest@sha256:db2d3c872d97299dfbb777d004264ed010aa577f62be581ab71624c88252fc5c
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
