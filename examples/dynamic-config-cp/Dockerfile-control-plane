FROM golang@sha256:a452d6273ad03a47c2f29b898d6bb57630e77baf839651ef77d03e4e049c5bf3

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get -qq update \
    && apt-get -qq install --no-install-recommends -y netcat \
    && apt-get -qq autoremove -y \
    && apt-get clean \
    && rm -rf /tmp/* /var/tmp/* /var/lib/apt/lists/*

RUN git clone https://github.com/envoyproxy/go-control-plane && cd go-control-plane && git checkout b4adc3bb5fe5288bff01cd452dad418ef98c676e
ADD ./resource.go /go/go-control-plane/internal/example/resource.go
RUN cd go-control-plane && make bin/example
WORKDIR /go/go-control-plane
