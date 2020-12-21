.. _building:


Building
========

The Envoy build system uses `Bazel <https://bazel.build/>`_.

In order to ease initial building and for a quick start, we provide an Ubuntu 16 based docker container that
has everything needed inside of it to build and *statically link* Envoy, see :repo:`ci/README.md`.

In order to build without using the Docker container, follow the instructions at :repo:`bazel/README.md`.

.. _install_requirements:

Requirements
------------

Envoy was initially developed and deployed on Ubuntu 14.04 LTS. It should work on any reasonably
recent Linux including Ubuntu 18.04 LTS.

Building Envoy has the following requirements:

* GCC 7+ or Clang/LLVM 7+ (for C++14 support). Clang/LLVM 9+ preferred where Clang is used (see below).
* These :repo:`Bazel native <bazel/repository_locations.bzl>` dependencies.

Please see the linked :repo:`CI <ci/README.md>` and :repo:`Bazel <bazel/README.md>` documentation
for more information on performing manual builds.
Please note that for Clang/LLVM 8 and lower, Envoy may need to be built with `--define tcmalloc=gperftools`
as the new tcmalloc code is not guaranteed to compile with lower versions of Clang.


Modifying Envoy
---------------

If you're interested in modifying Envoy and testing your changes, one approach
is to use Docker. This guide will walk through the process of building your own
Envoy binary, and putting the binary in an Ubuntu container.

.. toctree::
    :maxdepth: 2

    building/local_docker_build
