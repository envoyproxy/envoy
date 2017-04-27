Building
========

The Envoy build system uses Bazel. In order to ease initial building and for a quick start, we
provide an Ubuntu 16 based docker container that has everything needed inside of it to build
and *statically link* envoy, see :repo:`ci/README.md`.

In order to build manually, follow the instructions at :repo:`bazel/README.md`.
