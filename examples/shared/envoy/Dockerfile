ARG ENVOY_IMAGE="${ENVOY_IMAGE:-envoyproxy/envoy}"
ARG ENVOY_VARIANT="${ENVOY_VARIANT:-dev}"


FROM ${ENVOY_IMAGE}:${ENVOY_VARIANT} as envoy-base
ARG ENVOY_CONFIG=envoy.yaml
ENV ENVOY_CONFIG="$ENVOY_CONFIG"
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    rm -f /etc/apt/apt.conf.d/docker-clean \
    && echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' | tee /etc/apt/apt.conf.d/keep-cache \
    && apt-get -qq update -y \
    && apt-get -qq install --no-install-recommends -y curl
COPY --chmod=777 "$ENVOY_CONFIG" /etc/envoy.yaml
CMD ["/usr/local/bin/envoy", "-c", "/etc/envoy.yaml"]

FROM envoy-base as envoy-admin
ARG ENVOY_ADMIN_PORT=10001
ENV ENVOY_ADMIN_PORT="$ENVOY_ADMIN_PORT"
HEALTHCHECK \
    --interval=1s \
    --timeout=1s \
    --start-period=1s \
    --retries=3 \
    CMD curl -s "localhost:${ENVOY_ADMIN_PORT}/stats?filter=server.state" | grep 0 \
             && curl -s "localhost:${ENVOY_ADMIN_PORT}/stats?filter=listener_manager.workers_started" | grep 1

FROM envoy-base as envoy-fault-injection
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq update -y \
    && apt-get -qq install --no-install-recommends -y tree
COPY enable_delay_fault_injection.sh disable_delay_fault_injection.sh enable_abort_fault_injection.sh disable_abort_fault_injection.sh send_request.sh /


FROM envoy-base as envoy-jaeger-native
#
# for discussion on jaeger binary compatibility, and the source of the file, see here:
#  https://github.com/envoyproxy/envoy/issues/11382#issuecomment-638012072
#
RUN echo "4a7d17d4724ee890490bcd6cfdedb12a02316a3d33214348d30979abd201f1ca /usr/local/lib/libjaegertracing_plugin.so" > /tmp/checksum \
         && curl -Ls https://github.com/envoyproxy/misc/releases/download/jaegertracing-plugin/jaegertracing-plugin-centos.tar.gz \
              | tar zxf - -C /usr/local/lib \
         && mv /usr/local/lib/libjaegertracing.so.0.4.2 /usr/local/lib/libjaegertracing_plugin.so \
         && sha256sum -c /tmp/checksum \
         && rm /tmp/checksum


FROM envoy-base as envoy-load-balancing
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq update -y \
    && apt-get -qq install --no-install-recommends -y python3
COPY ./client.py /client.py
EXPOSE 8001


FROM envoy-base as envoy-double-proxy-base
COPY --chmod=777 ./certs/ca.crt /certs/cacert.pem


FROM envoy-double-proxy-base as envoy-double-proxy-frontend
COPY  --chmod=777 ./certs/postgres-frontend.example.com.crt /certs/clientcert.pem
COPY  --chmod=777 ./certs/example.com.key /certs/clientkey.pem


FROM envoy-double-proxy-base as envoy-double-proxy-backend
COPY --chmod=777 ./certs/postgres-backend.example.com.crt /certs/servercert.pem
COPY --chmod=777 ./certs/example.com.key /certs/serverkey.pem


FROM envoy-base as envoy-certs
COPY --chmod=777 ./certs /certs


FROM envoy-base as envoy-lua
ADD  --chmod=777 ./lib/mylibrary.lua /lib/mylibrary.lua


FROM envoy-base as envoy-go
ENV GODEBUG=cgocheck=0
COPY --chmod=777 ./lib/simple.so /lib/simple.so


FROM envoy-base as envoy-ext_authz
COPY --chmod=777 ./config /etc/envoy-config
COPY --chmod=777 ./run_envoy.sh /run_envoy.sh
CMD ["/bin/sh", "/run_envoy.sh"]


FROM envoy-base as envoy-dynamic-fs
COPY --chmod=777 ./configs /var/lib/envoy


FROM envoy-base
