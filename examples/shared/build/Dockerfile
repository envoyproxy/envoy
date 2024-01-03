FROM envoyproxy/envoy-build-ubuntu:fd9ec000fdd72d5c5e4e4ef16db4f9103058779e@sha256:1386a26f687826850ba488d66a6cd5337c5941b3b8793d08cfa6f9df12aa2fcf
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
