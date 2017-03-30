This directory contains a Dockerfile that builds an envoy image
on alpine as the base instead of the default ubuntu.

Invoking `make` will build the binary followed by the container.

Invoke `make binary` to build the envoy binary. This will take a while,
when invoked the first time, around 20-30 mins on a single socket
2.9 GHz + 16 GB 2GHz memory i7 system. It's recommended to build it on a
beefy machine for a quicker build.

Invoke `make container` to build the base alpine based Envoy container.
It will contain the envoy binary under /usr/local/bin/.

Invoke `make clean` to clean up the built bin directory.

The base alpine image is frolvlad/alpine-glibc, which has glibc
built into it.
