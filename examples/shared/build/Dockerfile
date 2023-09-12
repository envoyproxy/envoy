FROM envoyproxy/envoy-build-ubuntu:c243601cb7cd1ad9d6cd4cf5027aa71340fe54f8@sha256:1264adc62e7688af2d8f09276ea8a2987885e9f9202cf7324d44bba538a0ddbf
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
