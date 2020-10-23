.. _install:

Installing Envoy
================

The Envoy project :ref:`provides a number of pre-built Docker images <install_binaries>` for both ``amd64`` and ``arm64`` architectures.

The `Get Envoy <https://www.getenvoy.io/>`__ project also maintains a number of binaries
and repositories to accommodate many popular distributions.

If you are :ref:`installing on Mac OSX <start_install_macosx>`, you can install natively with ``brew``.

Once you have installed Envoy, check out the :ref:`quick start <start_quick_start>` guide for more information on
getting your Envoy proxy up and running.

Install Envoy on Debian GNU/Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on Debian <https://www.getenvoy.io/install/envoy/debian/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo apt update
   $ sudo apt install apt-transport-https ca-certificates curl gnupg2 software-properties-common
   $ curl -sL 'https://getenvoy.io/gpg' | sudo apt-key add -
   $ # verify the key
   $ apt-key fingerprint 6FF974DB | grep "5270 CEAC"
   $ sudo add-apt-repository "deb [arch=amd64] https://dl.bintray.com/tetrate/getenvoy-deb $(lsb_release -cs) stable"
   $ sudo apt update
   $ sudo apt install getenvoy-envoy

.. tip::

   To add the nightly repository instead, replace the word ``stable`` with ``nightly``,
   when adding the ``apt`` repository.

Install Envoy on Ubuntu Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on Ubuntu <https://www.getenvoy.io/install/envoy/ubuntu/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo apt update
   $ sudo apt install apt-transport-https ca-certificates curl gnupg-agent software-properties-common
   $ curl -sL 'https://getenvoy.io/gpg' | sudo apt-key add -
   $ # verify the key
   $ apt-key fingerprint 6FF974DB | grep "5270 CEAC"
   $ sudo add-apt-repository "deb [arch=amd64] https://dl.bintray.com/tetrate/getenvoy-deb $(lsb_release -cs) stable"
   $ sudo apt update
   $ sudo apt install -y getenvoy-envoy

.. tip::

   To add the nightly repository instead, replace the word ``stable`` with ``nightly``,
   when adding the ``apt`` repository.

Install Envoy on CentOS Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on CentOS <https://www.getenvoy.io/install/envoy/centos/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo yum install yum-utils
   $ sudo yum-config-manager --add-repo https://getenvoy.io/linux/centos/tetrate-getenvoy.repo
   $ sudo yum install getenvoy-envoy

.. tip::

   You can enable/disable ``nightly`` using ``yum-config-manager``:

   .. code-block:: console

      $ sudo yum-config-manager --enable tetrate-getenvoy-nightly
      $ sudo yum-config-manager --disable tetrate-getenvoy-nightly

Install Envoy on Redhat Enterprise Linux (RHEL)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can
`install Envoy on Redhat Enterprise Linux (RHEL) <https://www.getenvoy.io/install/envoy/rhel/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo yum install yum-utils
   $ sudo yum-config-manager --add-repo https://getenvoy.io/linux/rhel/tetrate-getenvoy.repo
   $ sudo yum install getenvoy-envoy

.. tip::

   You can enable/disable ``nightly`` using ``yum-config-manager``:

   .. code-block:: console

      $ sudo yum-config-manager --enable tetrate-getenvoy-nightly
      $ sudo yum-config-manager --disable tetrate-getenvoy-nightly

.. _start_install_macosx:

Install Envoy on Mac OSX
~~~~~~~~~~~~~~~~~~~~~~~~

You can install Envoy on Mac OSX using the official brew repositories, or from
`Get Envoy <https://www.getenvoy.io/install/envoy/macos>`__.

.. tabs::

   .. code-tab:: console brew

      $ brew update
      $ brew install envoy

   .. tab:: Get Envoy

      .. code-block:: console

	 $ brew tap tetratelabs/getenvoy
	 $ brew install envoy

      .. tip::

	 You can install the ``nightly`` version from
	 `Get Envoy <https://www.getenvoy.io/>`__ by adding the ``--HEAD`` flag to
	 the install command.

.. _start_install_docker:

Install Envoy using Docker
~~~~~~~~~~~~~~~~~~~~~~~~~~

You can run Envoy using the official Docker images, or by
using images provided by `Get Envoy <https://www.getenvoy.io/envoy/install/docker/>`__.

The following commands will pull and show the Envoy version of current images.

.. tabs::

   .. tab:: Envoy

      .. substitution-code-block:: console

	 $ docker pull envoyproxy/|envoy_docker_image|
	 $ docker run --rm envoyproxy/|envoy_docker_image| --version

   .. tab:: Get Envoy

      .. code-block:: console

	 $ docker pull getenvoy/envoy:stable
	 $ docker run --rm getenvoy/envoy:stable --version

      .. tip::

	 To use the ``nightly`` version from `Get Envoy <https://www.getenvoy.io/>`__
	 replace the word ``stable`` with ``nightly`` in the above commands.

.. _install_binaries:

Pre-built Envoy Docker images
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The following table shows the available Docker images

.. csv-table::
   :widths: 30 38 8 8 8 8
   :header-rows: 2
   :stub-columns: 1
   :file: _include/dockerhub-images.csv

.. note::

   In the above repositories, we tag a *vX.Y-latest* image for each security/stable release line.

   In the above *dev* repositories, the *latest* tag points to a container including the last
   Envoy build on master that passed tests.

   The Envoy project considers master to be release candidate quality at all times, and many
   organizations track and deploy master in production. We encourage you to do the same so that
   issues can be reported as early as possible in the development process.

   The ``envoy-build-ubuntu`` image does not contain a working Envoy server, but can be used for
   building Envoy and related containers. This image requires 4-5GB of available disk space to use.
