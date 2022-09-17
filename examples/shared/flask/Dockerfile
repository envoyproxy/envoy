FROM python:3.10.7-slim-bullseye@sha256:6de22c9cf887098265b7614582b00641c0c8c6735af538d0f267d6bb457634f1

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
