FROM envoyproxy/envoy-build-ubuntu:12959dce4ae3cfc1f52377d99e360e1e7873d338@sha256:9a0bb59faf29ce02f2cb9291318799e6fe5763af0067e4a06678689dd36f821b
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
