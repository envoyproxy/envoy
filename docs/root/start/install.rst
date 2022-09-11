.. _install:

Installing Envoy
================

The Envoy project :ref:`provides a number of pre-built Docker images <install_binaries>` for both ``amd64`` and ``arm64`` architectures.

If you are :ref:`installing on Mac OSX <start_install_macosx>`, you can install natively with ``brew``.

Once you have installed Envoy, check out the :ref:`quick start <start_quick_start>` guide for more information on
getting your Envoy proxy up and running.

Install Envoy on Debian GNU/Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on Debian using packages created by Tetrate <https://cloudsmith.io/~tetrate/repos/getenvoy-deb-stable/setup/#formats-deb>`_
until `official packages exist <https://github.com/envoyproxy/envoy/issues/16867>`_.

.. code-block:: console

   $ sudo apt update
   $ sudo apt install debian-keyring debian-archive-keyring apt-transport-https curl lsb-release
   $ curl -sL 'https://deb.dl.getenvoy.io/public/gpg.8115BA8E629CC074.key' | sudo gpg --dearmor -o /usr/share/keyrings/getenvoy-keyring.gpg
   # Verify the keyring - this should yield "OK"
   $ echo a077cb587a1b622e03aa4bf2f3689de14658a9497a9af2c427bba5f4cc3c4723 /usr/share/keyrings/getenvoy-keyring.gpg | sha256sum --check
   $ echo "deb [arch=amd64 signed-by=/usr/share/keyrings/getenvoy-keyring.gpg] https://deb.dl.getenvoy.io/public/deb/debian $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/getenvoy.list
   $ sudo apt update
   $ sudo apt install getenvoy-envoy

Install Envoy on Ubuntu Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on Ubuntu using packages created by Tetrate <https://cloudsmith.io/~tetrate/repos/getenvoy-deb-stable/setup/#formats-deb>`_
until `official packages exist <https://github.com/envoyproxy/envoy/issues/16867>`_.

.. code-block:: console

   $ sudo apt update
   $ sudo apt install apt-transport-https gnupg2 curl lsb-release
   $ curl -sL 'https://deb.dl.getenvoy.io/public/gpg.8115BA8E629CC074.key' | sudo gpg --dearmor -o /usr/share/keyrings/getenvoy-keyring.gpg
   # Verify the keyring - this should yield "OK"
   $ echo a077cb587a1b622e03aa4bf2f3689de14658a9497a9af2c427bba5f4cc3c4723 /usr/share/keyrings/getenvoy-keyring.gpg | sha256sum --check
   $ echo "deb [arch=amd64 signed-by=/usr/share/keyrings/getenvoy-keyring.gpg] https://deb.dl.getenvoy.io/public/deb/ubuntu $(lsb_release -cs) main" | sudo tee /etc/apt/sources.list.d/getenvoy.list
   $ sudo apt update
   $ sudo apt install -y getenvoy-envoy

Install Envoy on RPM-based distros
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can install Envoy on Centos/Redhat Enterprise Linux (RHEL) using `packages created by Tetrate <https://cloudsmith.io/~tetrate/repos/getenvoy-rpm-stable/setup/#formats-rpm>`_
until `official packages exist <https://github.com/envoyproxy/envoy/issues/16867>`_.

.. code-block:: console

   $ sudo yum install yum-utils
   $ sudo rpm --import 'https://rpm.dl.getenvoy.io/public/gpg.CF716AF503183491.key'
   $ curl -sL 'https://rpm.dl.getenvoy.io/public/config.rpm.txt?distro=el&codename=7' > /tmp/tetrate-getenvoy-rpm-stable.repo
   $ sudo yum-config-manager --add-repo '/tmp/tetrate-getenvoy-rpm-stable.repo'
   $ sudo yum makecache --disablerepo='*' --enablerepo='tetrate-getenvoy-rpm-stable'
   $ sudo yum install getenvoy-envoy

.. _start_install_macosx:

Install Envoy on Mac OSX
~~~~~~~~~~~~~~~~~~~~~~~~

You can install Envoy on Mac OSX using the official brew repositories.

.. tabs::

   .. code-tab:: console brew

      $ brew update
      $ brew install envoy

.. _start_install_windows:

Install Envoy on Windows
~~~~~~~~~~~~~~~~~~~~~~~~

You can run Envoy using the official Windows Docker image.

.. substitution-code-block:: console

   $ docker pull envoyproxy/|envoy_windows_docker_image|
   $ docker run --rm envoyproxy/|envoy_windows_docker_image| --version

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

.. _install_contrib:

Contrib images
~~~~~~~~~~~~~~

As described in `this document <https://docs.google.com/document/d/1yl7GOZK1TDm_7vxQvt8UQEAu07UQFru1uEKXM6ZZg_g/edit#>`_,
the Envoy project allows extensions to enter the repository as "contrib" extensions. The requirements
for such extensions are lower, and as such they are only available by default in special images.
The `envoyproxy/envoy-contrib <https://hub.docker.com/r/envoyproxy/envoy-contrib/tags/>`_ image
contains all contrib extensions on top of an Ubuntu base. The
`envoyproxy/envoy-contrib-debug <https://hub.docker.com/r/envoyproxy/envoy-contrib-debug/tags/>`_
image contains all contrib extensions on top of an Ubuntu base as well as debug symbols. Throughout
the documentation, extensions are clearly marked as being a contrib extension or a core extension.

.. _install_tools:

Tools images
~~~~~~~~~~~~

The Envoy project ships images for tools that are separate from the proxy binary but are useful
in supporting systems such as CI, configuration generation pipelines, etc. Currently installed
tools in ``/usr/local/bin`` include:

* :ref:`Schema validator check tool <install_tools_schema_validator_check_tool>`

.. _install_binaries:

Pre-built Envoy Docker images
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following table shows the available Docker images

.. list-table::
   :widths: auto
   :header-rows: 2
   :stub-columns: 1

   * -
     -
     - stable
     - stable
     - main
     - main
   * - Docker image
     - Description
     - amd64
     - arm64
     - amd64
     - arm64
   * - `envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy/tags/>`_
     - Release binary with symbols stripped on top of an Ubuntu 20.04 base.
     - |DOCKER_IMAGE_TAG_NAME|
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
   * - `envoyproxy/envoy-contrib <https://hub.docker.com/r/envoyproxy/envoy-contrib/tags/>`_
     - Release :ref:`contrib <install_contrib>` binary with symbols stripped on top of an Ubuntu 20.04 base.
     - |DOCKER_IMAGE_TAG_NAME|
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
   * - `envoyproxy/envoy-distroless <https://hub.docker.com/r/envoyproxy/envoy-distroless/tags/>`_
     - Release binary with symbols stripped on top of a distroless base.
     - |DOCKER_IMAGE_TAG_NAME|
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
   * - `envoyproxy/envoy-windows <https://hub.docker.com/r/envoyproxy/envoy-windows/tags/>`_
     - Release binary with symbols stripped on top of a Windows Server 1809 base.
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
     -
   * - `envoyproxy/envoy-debug <https://hub.docker.com/r/envoyproxy/envoy-debug/tags/>`_
     - Release binary with debug symbols on top of an Ubuntu 20.04 base.
     - |DOCKER_IMAGE_TAG_NAME|
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
   * - `envoyproxy/envoy-contrib-debug <https://hub.docker.com/r/envoyproxy/envoy-contrib-debug/tags/>`_
     - Release :ref:`contrib <install_contrib>` binary with debug symbols on top of an Ubuntu 20.04 base.
     - |DOCKER_IMAGE_TAG_NAME|
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
   * - `envoyproxy/envoy-tools <https://hub.docker.com/r/envoyproxy/envoy-tools/tags/>`_
     - Release :ref:`tools <install_tools>` on top of an Ubuntu 20.04 base.
     - |DOCKER_IMAGE_TAG_NAME|
     - |DOCKER_IMAGE_TAG_NAME|
     -
     -
   * - `envoyproxy/envoy-dev <https://hub.docker.com/r/envoyproxy/envoy-dev/tags/>`_
     - Release binary with symbols stripped on top of an Ubuntu 20.04 base.
     -
     -
     - latest
     - latest
   * - `envoyproxy/envoy-contrib-dev <https://hub.docker.com/r/envoyproxy/envoy-contrib-dev/tags/>`_
     - Release :ref:`contrib <install_contrib>` binary with symbols stripped on top of an Ubuntu 20.04 base.
     -
     -
     - latest
     - latest
   * - `envoyproxy/envoy-distroless-dev <https://hub.docker.com/r/envoyproxy/envoy-distroless-dev/tags/>`_
     - Release binary with symbols stripped on top of a distroless base.
     -
     -
     - latest
     - latest
   * - `envoyproxy/envoy-debug-dev <https://hub.docker.com/r/envoyproxy/envoy-debug-dev/tags/>`_
     - Release binary with debug symbols on top of an Ubuntu 20.04 base.
     -
     -
     - latest
     - latest
   * - `envoyproxy/envoy-contrib-debug-dev <https://hub.docker.com/r/envoyproxy/envoy-contrib-debug-dev/tags/>`_
     - Release :ref:`contrib <install_contrib>` binary with debug symbols on top of an Ubuntu 20.04 base.
     -
     -
     - latest
     - latest
   * - `envoyproxy/envoy-windows-dev <https://hub.docker.com/r/envoyproxy/envoy-windows-dev/tags/>`_
     - Release binary with symbols stripped on top of a Windows Server 1809 base. Includes build tools.
     -
     -
     - latest
     -
   * - `envoyproxy/envoy-tools-dev <https://hub.docker.com/r/envoyproxy/envoy-tools-dev/tags/>`_
     - Release :ref:`tools <install_tools>` on top of an Ubuntu 20.04 base.
     -
     -
     - latest
     - latest
   * - `envoyproxy/envoy-build-ubuntu <https://hub.docker.com/r/envoyproxy/envoy-build-ubuntu/tags/>`_
     - Build image which includes tools for building multi-arch Envoy and containers.
     -
     -
     - See Docker Hub
     - See Docker Hub

.. note::

   In the above repositories, we tag a *vX.Y-latest* image for each security/stable release line.

   In the above *dev* repositories, the *latest* tag points to a container including the last
   Envoy build on main that passed tests.

   The Envoy project considers main to be release candidate quality at all times, and many
   organizations track and deploy main in production. We encourage you to do the same so that
   issues can be reported as early as possible in the development process.

   The ``envoy-build-ubuntu`` image does not contain a working Envoy server, but can be used for
   building Envoy and related containers. This image requires 4-5GB of available disk space to use.

   All the docker images are available in Docker Hub, but `its rate limit policy <https://www.docker.com/increase-rate-limits>`_
   doesn't apply to users since the "envoyproxy" namespace is allowlisted.
