FROM envoyproxy/envoy-build-ubuntu:7d47b66b7c49c5336a1cd0ba4bdb75dd730c62d6@sha256:ec1c62a75d3ca65b4fbef6f3c9664211e1a538eb699553f09e34774666cb7641
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
