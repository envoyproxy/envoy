FROM python:3.10.7-slim-bullseye@sha256:f2ee145f3bc4e061f8dfe7e6ebd427a410121495a0bd26e7622136db060c59e0 as python-base


FROM python-base as python-grpc-client
WORKDIR /client
COPY requirements.txt /client/requirements.txt
RUN pip install --require-hashes -qr /client/requirements.txt
# Copy the sources, including the stubs
COPY client.py /client/grpc-kv-client.py
COPY kv /client/kv
RUN chmod a+x /client/grpc-kv-client.py
CMD tail -f /dev/null


FROM python-base as aiohttp-service
ENV DEBIAN_FRONTEND=noninteractive
ADD requirements.txt /tmp/requirements.txt
RUN pip3 install -qr /tmp/requirements.txt \
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


FROM aiohttp-service as aiohttp-tracing-service
ADD tracing/service.py /code/service.py


FROM aiohttp-service as aiohttp-tracing-service2
ADD tracing/service2.py /code/service.py


FROM aiohttp-service as aiohttp-tracing-service3
COPY --from=envoyproxy/envoy:dev /usr/local/bin/envoy /usr/local/bin/envoy
ADD tracing/service2.py /code/service.py
ADD tracing/start_service.sh /usr/local/bin/start_service.sh
RUN chmod u+x /usr/local/bin/start_service.sh
ENTRYPOINT ["/usr/local/bin/start_service.sh"]


FROM aiohttp-tracing-service3 as aiohttp-jaeger-service
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


FROM aiohttp-service as aiohttp-hello-service
ADD service.py /code/service.py


FROM aiohttp-service as aiohttp-data-service
RUN mkdir -p /code/data
RUN dd if=/dev/zero of="/code/data/file.txt" bs=1024 count=10240 \
    && dd if=/dev/zero of="/code/data/file.json" bs=1024 count=10240
ADD data-service.py /code/service.py


FROM aiohttp-service as aiohttp-postgres-service
ADD postgres/requirements.txt /tmp/requirements.txt
RUN pip3 install -qr /tmp/requirements.txt
