FROM envoyproxy/envoy-build-ubuntu:94e5d873c145ae86f205117e76276161c9af4806@sha256:8d3763e19d5b71fdc95666d75073ce4581e566ce28ca09106607b6a3ef7ba902
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    # apt-get -qq update \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
