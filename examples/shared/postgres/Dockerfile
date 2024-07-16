FROM postgres:latest@sha256:f08a57b1e922be40565e1497dce1f60fd0cac9a26a2408695decd5a312f93978
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
