FROM postgres:latest@sha256:8a2975ef8f47391d186cfe43b38d4ccef88b27e40397ae6ad3b961ffacb57029
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
