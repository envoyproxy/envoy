.. _building:


Building
========

The Envoy build system uses Bazel. In order to ease initial building and for a quick start, we
provide an Ubuntu 16 based docker container that has everything needed inside of it to build
and *statically link* envoy, see :repo:`ci/README.md`.

In order to build manually, follow the instructions at :repo:`bazel/README.md`.

.. _install_requirements:

Requirements
------------

Envoy was initially developed and deployed on Ubuntu 14 LTS. It should work on any reasonably
recent Linux including Ubuntu 16 LTS.

Building Envoy has the following requirements:

* GCC 5+ (for C++14 support).
* These :repo:`pre-built </ci/build_container/build_recipes>` third party dependencies.
* These :repo:`Bazel native <bazel/repository_locations.bzl>` dependencies.

Please see the linked :repo:`CI <ci/README.md>` and :repo:`Bazel <bazel/README.md>` documentation
for more information on performing manual builds.

.. _install_binaries:

Pre-built binaries
------------------

On every master commit we create a set of lightweight Docker images that contain the Envoy
binary. We also tag the docker images with release versions when we do official releases.

* `envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy/tags/>`_: Release binary with
  symbols stripped on top of an Ubuntu Xenial base.
* `envoyproxy/envoy-alpine <https://hub.docker.com/r/envoyproxy/envoy-alpine/tags/>`_: Release
  binary with symbols stripped on top of a **glibc** alpine base.
* `envoyproxy/envoy-alpine-debug <https://hub.docker.com/r/envoyproxy/envoy-alpine-debug/tags/>`_:
  Release binary with debug symbols on top of a **glibc** alpine base.

We will consider producing additional binary types depending on community interest in helping with
CI, packaging, etc. Please open an `issue <https://github.com/envoyproxy/envoy/issues>`_ in GitHub
if desired.

Modifying Envoy
---------------

If you're interested in modifying Envoy and testing your changes, one approach
is to use Docker. This guide will walk through the process of building your own
Envoy binary, and putting the binary in an Ubuntu container.

.. toctree::
    :maxdepth: 1

    sandboxes/local_docker_build



