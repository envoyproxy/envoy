FROM envoyproxy/envoy-build-ubuntu:7304f974de2724617b7492ccb4c9c58cd420353a

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y -qq gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
