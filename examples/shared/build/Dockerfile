FROM envoyproxy/envoy-build-ubuntu:458cb49ca2013c0ccf057d00ad1d4407920c4e52

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y -qq gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
