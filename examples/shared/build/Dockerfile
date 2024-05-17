FROM envoyproxy/envoy-build-ubuntu:75238004b0fcfd8a7f71d380d7a774dda5c39622@sha256:4ceec2b488c82320ffe66dd4c42f791c941f15dd021aeb3649e120c1ca675896
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
