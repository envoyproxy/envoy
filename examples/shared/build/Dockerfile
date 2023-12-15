FROM envoyproxy/envoy-build-ubuntu:6eba113ee1a0ef8e4f71830e90b6aedbbeb7360c@sha256:d117d6139f3af1eede6bb1606ad05ffccb766eef3262b336dd31bcf02a81a669
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
