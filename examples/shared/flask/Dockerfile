FROM python:3.10.7-slim-bullseye@sha256:f2ee145f3bc4e061f8dfe7e6ebd427a410121495a0bd26e7622136db060c59e0 as shared-flask

ENV DEBIAN_FRONTEND=noninteractive

ADD requirements.txt /tmp/flask-requirements.txt
RUN pip3 install -qr /tmp/flask-requirements.txt \
    && apt-get -qq update \
    && apt-get -qq install -y --no-install-recommends netcat \
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


FROM shared-flask as data-flask

RUN mkdir -p /code/data
RUN dd if=/dev/zero of="/code/data/file.txt" bs=1024 count=10240 \
    && dd if=/dev/zero of="/code/data/file.json" bs=1024 count=10240


FROM shared-flask as postgres-flask
ADD postgres/requirements.txt /tmp/requirements.txt
RUN pip3 install -qr /tmp/requirements.txt


FROM shared-flask as tracing-flask

COPY --from=envoyproxy/envoy-dev:latest /usr/local/bin/envoy /usr/local/bin/envoy

ADD tracing/requirements.txt /tmp/requirements.txt
ADD tracing/service.py /code/service.py
ADD tracing/start_service.sh /usr/local/bin/start_service.sh

RUN pip3 install -qr /tmp/requirements.txt \
    && chmod u+x /usr/local/bin/start_service.sh

ENTRYPOINT ["/usr/local/bin/start_service.sh"]


FROM tracing-flask as jaeger-flask

RUN apt-get -qq update && apt-get -qq install --no-install-recommends -y curl

#
# for discussion on jaeger binary compatibility, and the source of the file, see here:
#  https://github.com/envoyproxy/envoy/issues/11382#issuecomment-638012072
#
RUN echo "4a7d17d4724ee890490bcd6cfdedb12a02316a3d33214348d30979abd201f1ca  /usr/local/lib/libjaegertracing_plugin.so" > /tmp/checksum \
         && curl -Ls https://github.com/envoyproxy/misc/releases/download/jaegertracing-plugin/jaegertracing-plugin-centos.tar.gz \
              | tar zxf - -C /usr/local/lib \
         && mv /usr/local/lib/libjaegertracing.so.0.4.2 /usr/local/lib/libjaegertracing_plugin.so \
         && sha256sum -c /tmp/checksum \
         && rm /tmp/checksum


# This must be last!
FROM shared-flask
