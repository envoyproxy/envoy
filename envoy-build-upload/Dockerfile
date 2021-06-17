FROM ubuntu:bionic

ENV DEBIAN_FRONTEND=noninteractive

# Google Cloud SDK
RUN apt-get update \
    && apt-get install -y -qq --no-install-recommends ca-certificates curl git gnupg sudo \
    && echo "deb [signed-by=/usr/share/keyrings/cloud.google.gpg] http://packages.cloud.google.com/apt cloud-sdk main" \
          | tee -a /etc/apt/sources.list.d/google-cloud-sdk.list \
    && curl https://packages.cloud.google.com/apt/doc/apt-key.gpg | \
           apt-key --keyring /usr/share/keyrings/cloud.google.gpg add - \
    && apt-get update \
    && apt-get install -y -qq --no-install-recommends google-cloud-sdk \
    && groupadd -r pcap \
    && apt-get autoremove -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* \
    && rm -rf /var/lib/apt/lists/*
