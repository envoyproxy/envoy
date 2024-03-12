FROM envoyproxy/envoy-build-ubuntu:f94a38f62220a2b017878b790b6ea98a0f6c5f9c@sha256:2dd96b6f43c08ccabd5f4747fce5854f5f96af509b32e5cf6493f136e9833649
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
