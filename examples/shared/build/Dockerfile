FROM envoyproxy/envoy-build-ubuntu:321658b6b50abda6869f89fac275f59bf3b1e757
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -qq update \
    && apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
