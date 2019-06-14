#!/bin/bash -e

# buildifier
VERSION=0.25.0
SHA256=6e6aea35b2ea2b4951163f686dfbfe47b49c840c56b873b3a7afe60939772fc1
curl --location --output /usr/local/bin/buildifier https://github.com/bazelbuild/buildtools/releases/download/"$VERSION"/buildifier \
  && echo "$SHA256" '/usr/local/bin/buildifier' | sha256sum --check \
  && chmod +x /usr/local/bin/buildifier
