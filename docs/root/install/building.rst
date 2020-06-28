.. _building:


Building
========

The Envoy build system uses Bazel. In order to ease initial building and for a quick start, we
provide an Ubuntu 16 based docker container that has everything needed inside of it to build
and *statically link* Envoy, see :repo:`ci/README.md`.

In order to build manually, follow the instructions at :repo:`bazel/README.md`.

.. _install_requirements:

Requirements
------------

Envoy was initially developed and deployed on Ubuntu 14.04 LTS. It should work on any reasonably
recent Linux including Ubuntu 18.04 LTS.

Building Envoy has the following requirements:

* GCC 7+ or Clang/LLVM 7+ (for C++14 support).
* These :repo:`Bazel native <bazel/repository_locations.bzl>` dependencies.

Please see the linked :repo:`CI <ci/README.md>` and :repo:`Bazel <bazel/README.md>` documentation
for more information on performing manual builds.

.. _install_binaries:

Pre-built binaries
------------------

We build and tag Docker images with release versions when we do official releases. These images can
be found in the following repositories:

* `envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy/tags/>`_: Release binary with
  symbols stripped on top of an Ubuntu Bionic base.
* `envoyproxy/envoy-alpine <https://hub.docker.com/r/envoyproxy/envoy-alpine/tags/>`_: Release
  binary with symbols stripped on top of a **glibc** alpine base.
* `envoyproxy/envoy-alpine-debug <https://hub.docker.com/r/envoyproxy/envoy-alpine-debug/tags/>`_:
  Release binary with debug symbols on top of a **glibc** alpine base.

.. note::

  In the above repositories, we tag a *vX.Y-latest* image for each security/stable release line.

On every master commit we additionally create a set of development Docker images. These images can
be found in the following repositories:

* `envoyproxy/envoy-dev <https://hub.docker.com/r/envoyproxy/envoy-dev/tags/>`_: Release binary with
  symbols stripped on top of an Ubuntu Bionic base.
* `envoyproxy/envoy-alpine-dev <https://hub.docker.com/r/envoyproxy/envoy-alpine-dev/tags/>`_: Release
  binary with symbols stripped on top of a **glibc** alpine base.
* `envoyproxy/envoy-alpine-debug-dev <https://hub.docker.com/r/envoyproxy/envoy-alpine-debug-dev/tags/>`_:
  Release binary with debug symbols on top of a **glibc** alpine base.

In the above *dev* repositories, the *latest* tag points to the last Envoy SHA in master that passed
tests.

.. note::

  The Envoy project considers master to be release candidate quality at all times, and many
  organizations track and deploy master in production. We encourage you to do the same so that
  issues can be reported as early as possible in the development process.

Packaged Envoy pre-built binaries for a variety of platforms are available via
`GetEnvoy.io <https://www.getenvoy.io/>`_.

We will consider producing additional binary types depending on community interest in helping with
CI, packaging, etc. Please open an `issue in GetEnvoy <https://github.com/tetratelabs/getenvoy/issues>`_
for pre-built binaries for different platforms.

Modifying Envoy
---------------

If you're interested in modifying Envoy and testing your changes, one approach
is to use Docker. This guide will walk through the process of building your own
Envoy binary, and putting the binary in an Ubuntu container.

.. toctree::
    :maxdepth: 2

    sandboxes/local_docker_build
