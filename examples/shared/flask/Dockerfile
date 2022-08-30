FROM python:3.10.6-slim-bullseye@sha256:dff7fd9200421a8c65e020af221a21c8aab784c5c8a8d55c64a095b645209d77

ENV DEBIAN_FRONTEND=noninteractive

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt \
    && apt-get update \
    && apt-get install -y -qq --no-install-recommends netcat \
    && apt-get -qq autoremove -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir /code

HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=3 \
    CMD nc -zv localhost 8080

ENTRYPOINT ["python3", "/code/service.py"]
