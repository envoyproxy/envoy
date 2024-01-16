FROM envoyproxy/envoy-build-ubuntu:0ca52447572ee105a4730da5e76fe47c9c5a7c64@sha256:d736c58f06f36848e7966752cc7e01519cc1b5101a178d5c6634807e8ac3deab
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
