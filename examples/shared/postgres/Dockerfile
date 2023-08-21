FROM postgres:latest@sha256:992c5398ca50c716e8c2556a6f6733a0e471e178a6320478a86bb4abe9566299
COPY docker-healthcheck.sh /usr/local/bin/
HEALTHCHECK CMD ["docker-healthcheck.sh"]
