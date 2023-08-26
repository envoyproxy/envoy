FROM envoyproxy/envoy-build-ubuntu:52aebdbfda0c3704bd291d1163b4e1c78d6db9f2@sha256:2385bc148e28b11d6a5444bae2fbe347743004d3ae0ac13bf579a684412fe70c
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
