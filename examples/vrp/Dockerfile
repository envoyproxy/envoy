ARG ENVOY_VRP_BASE_IMAGE=envoyproxy/envoy-dev


# STAGE: envoy-google-vrp-base
FROM ${ENVOY_VRP_BASE_IMAGE} AS envoy-google-vrp-base
EXPOSE 10000
EXPOSE 10001
CMD ["supervisord", "-c", "/etc/supervisor.conf"]
ENTRYPOINT []
ADD configs/google-vrp/envoy-edge.yaml /etc/envoy/envoy-edge.yaml
ADD configs/google-vrp/envoy-origin.yaml /etc/envoy/envoy-origin.yaml
ADD configs/google-vrp/launch_envoy.sh /usr/local/bin/launch_envoy.sh
ADD test/config/integration/certs/serverkey.pem /etc/envoy/certs/serverkey.pem
ADD test/config/integration/certs/servercert.pem /etc/envoy/certs/servercert.pem
RUN apt-get -qq update \
    && apt-get -qq upgrade -y \
    && apt-get -qq install -y libc++1 supervisor gdb strace tshark \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* \
    && rm -rf /var/lib/apt/lists/*
ADD configs/google-vrp/supervisor.conf /etc/supervisor.conf
RUN chmod 777 /var/log/supervisor
RUN chmod a+r /etc/supervisor.conf /etc/envoy/* /etc/envoy/certs/*
RUN chmod a+rx /usr/local/bin/launch_envoy.sh


# STAGE: envoy-google-vrp
FROM envoy-google-vrp-base as envoy-google-vrp
COPY --from=binary /usr/local/bin/envoy /usr/local/bin/envoy


# STAGE: envoy-google-vrp-custom
FROM envoy-google-vrp-base as envoy-google-vrp-custom
ARG ENVOY_CTX_BINARY_PATH
ADD "${ENVOY_CTX_BINARY_PATH}" /usr/local/bin/envoy
