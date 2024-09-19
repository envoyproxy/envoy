.. _install:

Installing Envoy
================

The Envoy project :ref:`provides a number of pre-built Docker images <install_binaries>` for both ``amd64`` and ``arm64`` architectures.

If you are :ref:`installing on Mac OSX <start_install_macosx>`, you can install natively with ``brew``.

Once you have installed Envoy, check out the :ref:`quick start <start_quick_start>` guide for more information on
getting your Envoy proxy up and running.

Install Envoy on Debian-based Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

 If you are using a different deb-based distribution to the ones shown below, you may still be able to use one of them.

.. tabs::

   .. code-tab:: console Debian bookworm

      $ wget -O- https://apt.envoyproxy.io/signing.key | sudo gpg --dearmor -o /etc/apt/keyrings/envoy-keyring.gpg
      $ echo "deb [signed-by=/etc/apt/keyrings/envoy-keyring.gpg] https://apt.envoyproxy.io bookworm main" | sudo tee /etc/apt/sources.list.d/envoy.list
      $ sudo apt-get update
      $ sudo apt-get install envoy
      $ envoy --version

   .. code-tab:: console Debian bullseye

      $ wget -O- https://apt.envoyproxy.io/signing.key | sudo gpg --dearmor -o /etc/apt/keyrings/envoy-keyring.gpg
      $ echo "deb [signed-by=/etc/apt/keyrings/envoy-keyring.gpg] https://apt.envoyproxy.io bullseye main" | sudo tee /etc/apt/sources.list.d/envoy.list
      $ sudo apt-get update
      $ sudo apt-get install envoy
      $ envoy --version

   .. code-tab:: console Ubuntu focal

      $ wget -O- https://apt.envoyproxy.io/signing.key | sudo gpg --dearmor -o /etc/apt/keyrings/envoy-keyring.gpg
      $ echo "deb [signed-by=/etc/apt/keyrings/envoy-keyring.gpg] https://apt.envoyproxy.io focal main" | sudo tee /etc/apt/sources.list.d/envoy.list
      $ sudo apt-get update
      $ sudo apt-get install envoy
      $ envoy --version

   .. code-tab:: console Ubuntu jammy

      $ wget -O- https://apt.envoyproxy.io/signing.key | sudo gpg --dearmor -o /etc/apt/keyrings/envoy-keyring.gpg
      $ echo "deb [signed-by=/etc/apt/keyrings/envoy-keyring.gpg] https://apt.envoyproxy.io jammy main" | sudo tee /etc/apt/sources.list.d/envoy.list
      $ sudo apt-get update
      $ sudo apt-get install envoy
      $ envoy --version

.. _start_install_macosx:

Install Envoy on Mac OSX
~~~~~~~~~~~~~~~~~~~~~~~~

You can install Envoy on Mac OSX using the official brew repositories.

.. tabs::

   .. code-tab:: console brew

      $ brew update
      $ brew install envoy

.. _start_install_docker:

Install Envoy using Docker
~~~~~~~~~~~~~~~~~~~~~~~~~~

You can run Envoy using the official Docker images.

The following commands will pull and show the Envoy version of current images.

.. tabs::

   .. tab:: Envoy

      .. substitution-code-block:: console

         $ docker pull envoyproxy/|envoy_docker_image|
         $ docker run --rm envoyproxy/|envoy_docker_image| --version

   .. tab:: Envoy (distroless)

      .. substitution-code-block:: console

         $ docker pull envoyproxy/|envoy_distroless_docker_image|
         $ docker run --rm envoyproxy/|envoy_distroless_docker_image| --version


Supported tags
^^^^^^^^^^^^^^

For stable Envoy versions images are created for the version and the latest of that minor version.

For example, if the latest version in the v1.73.x series is v1.73.7 then images are created for:

- ``envoyproxy/envoy:v1.73.7``
- ``envoyproxy/envoy:v1.73-latest``

A similar strategy is used to create images for each of the versioned variants.

Supported architectures
^^^^^^^^^^^^^^^^^^^^^^^

The Envoy project currently supports ``amd64`` and ``arm64`` architectures for its Linux build and images.

.. _install_contrib:

Contrib builds
^^^^^^^^^^^^^^
As described in `this document <https://docs.google.com/document/d/1yl7GOZK1TDm_7vxQvt8UQEAu07UQFru1uEKXM6ZZg_g/edit#>`_,
the Envoy project allows extensions to enter the repository as "contrib" extensions. The requirements
for such extensions are lower, and as such they are only available by default in special images.

Throughout the documentation, extensions are clearly marked as being a contrib extension or a core extension.

Image variants
^^^^^^^^^^^^^^

``envoyproxy/envoy:<version>``
++++++++++++++++++++++++++++++

These images contains just the core Envoy binary built upon an Ubuntu base image.

``envoyproxy/envoy:distroless-<version>``
+++++++++++++++++++++++++++++++++++++++++

These images contains just the core Envoy binary built upon a distroless (nonroot/nossl) base image.

These images are the most efficient and secure way to deploy Envoy in a container.

``envoyproxy/envoy:contrib-<version>``
++++++++++++++++++++++++++++++++++++++

These images contain the Envoy binary built with all contrib extensions on top of an Ubuntu base.

``envoyproxy/envoy:tools-<version>``
++++++++++++++++++++++++++++++++++++

These images contain tools that are separate from the proxy binary but are useful in supporting systems such as CI, configuration generation pipelines, etc

``envoyproxy/envoy:dev`` / ``envoyproxy/envoy:dev-<SHA>`` / ``envoyproxy/envoy:<variant>-dev`` / ``envoyproxy/envoy:<variant>-dev-<SHA>``
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

The Envoy project considers the ``main`` branch to be release candidate quality at all times, and many organizations track and deploy ``main`` in production.

We encourage you to do the same so that issues can be reported and resolved as quickly as possible.


``envoyproxy/envoy:debug-<version>`` / ``envoyproxy/envoy:<variant>-debug-<version>``
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

These images are built for each of the variants, but with an Envoy binary containing debug symbols.

.. _install_binaries:

Pre-built Envoy Docker images
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

`envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy>`__
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

The following table shows the available Docker tag variants for the latest
`envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy>`__ images.

.. list-table::
   :widths: auto
   :header-rows: 1
   :stub-columns: 1

   * - variant
     - latest stable (amd64/arm64)
     - main dev (amd64/arm64)
   * - envoy (default)
     - :dockerhub_envoy:`envoy`
     - :dockerhub_envoy:`envoy-dev`
   * - contrib
     - :dockerhub_envoy:`contrib`
     - :dockerhub_envoy:`contrib-dev`
   * - distroless
     - :dockerhub_envoy:`distroless`
     - :dockerhub_envoy:`distroless-dev`
   * - debug
     - :dockerhub_envoy:`debug`
     - :dockerhub_envoy:`debug-dev`
   * - contrib-debug
     - :dockerhub_envoy:`contrib-debug`
     - :dockerhub_envoy:`contrib-debug-dev`
   * - tools
     - :dockerhub_envoy:`tools`
     - :dockerhub_envoy:`tools-dev`


.. _install_tools:

`envoyproxy/envoy-build-ubuntu <https://hub.docker.com/r/envoyproxy/envoy-build-ubuntu>`__
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

Build images are always versioned using their commit SHA, which is in turn committed to the Envoy repository
to ensure reproducible builds.

.. list-table::
   :widths: auto
   :header-rows: 1
   :stub-columns: 1

   * - variant
     - latest (amd64/arm64)
   * - envoy-build-ubuntu (default)
     - :dockerhub_envoy:`build-ubuntu`
   * - envoy-build-ubuntu:mobile
     - :dockerhub_envoy:`build-ubuntu-mobile`

.. note::
   The ``envoy-build-ubuntu`` image does not contain a working Envoy server, but can be used for
   building Envoy and related containers.

   This image requires 4-5GB of available disk space to use.
