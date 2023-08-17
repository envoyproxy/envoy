FROM postgres:latest@sha256:422232012889e0c0f52b561558876ecb8f6a881ca01acc879b23c3684a0ee0ed
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
