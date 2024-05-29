.. _building:


Building
========

The Envoy build system uses `Bazel <https://bazel.build/>`_.

In order to ease initial building and for a quick start, we provide a recent Ubuntu-based docker container
that has everything needed inside of it to build and *statically link* Envoy, see :repo:`ci/README.md`.

In order to build without using the Docker container, follow the instructions at :repo:`bazel/README.md`.

.. _install_requirements:

Linux/Mac Target Requirements
-----------------------------

Envoy was initially developed and deployed on Ubuntu 14.04 LTS. It should work on any reasonably
recent Linux including Ubuntu 20.04 LTS.

Building Envoy has the following requirements:

* Recent GCC/Clang versions - please see :repo:`bazel/README.md#supported-compiler-versions` for current requirements.
* About 2GB of RAM per core (so 32GB of RAM for 8 cores with hyperthreading). See
  :ref:`this FAQ entry <faq_build_speed>` for more information on build performance.
* These :repo:`Bazel native <bazel/repository_locations.bzl>` dependencies.

Please note that for Clang/LLVM 8 and lower, Envoy may need to be built with ``--define tcmalloc=gperftools``
as the new tcmalloc code is not guaranteed to compile with lower versions of Clang.


Windows Target Requirements
---------------------------

.. include:: ../_include/windows_support_ended.rst

Envoy supports Windows as a target platform. The requirements below only apply if you want to build the Windows
native executable. If you want to build the Linux version of Envoy on Windows either with WSL or Linux containers
please see the Linux requirements above.

Building Envoy for Windows has the following requirements:

* A Windows (virtual) machine running on version 1903 (10.0.18362.1) and above.
* The Windows 10 SDK, version 1803 (10.0.17134.12). Some features may require a newer SDK.
* `Build Tools for Visual Studio 2019 <https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2019>`_
* The `MSYS2 toolchain <https://www.msys2.org/>`_
* These :repo:`Bazel native <bazel/repository_locations.bzl>` dependencies.

Detailed instructions
---------------------

Please see :repo:`developer use of CI Docker images <ci/README.md>` and :repo:`building Envoy with Bazel <bazel/README.md>`
documentation for more information on performing manual builds.

Modifying Envoy
---------------

If you're interested in modifying Envoy and testing your changes, one approach
is to use Docker. This guide will walk through the process of building your own
Envoy binary, and putting the binary in an Ubuntu container.

.. toctree::
    :maxdepth: 2

    building/local_docker_build
