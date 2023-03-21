FROM envoyproxy/envoy-build-ubuntu:458cb49ca2013c0ccf057d00ad1d4407920c4e52
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get -qq update \
    && apt-get -qq install --no-install-recommends -y gosu \
    && groupadd -f envoygroup \
    && useradd -g envoygroup -m -d /home/envoybuild envoybuild
