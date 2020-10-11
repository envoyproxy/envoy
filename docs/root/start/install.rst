.. _install:

Installing Envoy
================

The `Get Envoy <https://www.getenvoy.io/>`__ project maintains a number of binaries
and repositories to accommodate many popular distributions.

Once you have installed Envoy, you can check which version you have with:

.. code-block:: console

   $ envoy --version

Install Envoy on Debian Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on Debian <https://www.getenvoy.io/install/envoy/debian/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo apt update
   $ sudo apt install -y apt-transport-https ca-certificates curl gnupg2 software-properties-common
   $ curl -sL 'https://getenvoy.io/gpg' | sudo apt-key add -
   $ # verify the key
   $ apt-key fingerprint 6FF974DB | grep "5270 CEAC"
   $ sudo add-apt-repository "deb [arch=amd64] https://dl.bintray.com/tetrate/getenvoy-deb $(lsb_release -cs) stable"

.. tip::

   To add the nightly repository instead, replace the word ``stable`` with ``nightly``,
   when adding the ``apt`` repository.

Install Envoy on Ubuntu Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on Ubuntu <https://www.getenvoy.io/install/envoy/ubuntu/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo apt update
   $ sudo apt install -y apt-transport-https ca-certificates curl gnupg-agent software-properties-common
   $ curl -sL 'https://getenvoy.io/gpg' | sudo apt-key add -
   $ # verify the key
   $ apt-key fingerprint 6FF974DB | grep "5270 CEAC"
   $ sudo add-apt-repository "deb [arch=amd64] https://dl.bintray.com/tetrate/getenvoy-deb $(lsb_release -cs) stable"
   $ sudo apt update && sudo apt install -y getenvoy-envoy

.. tip::

   To add the nightly repository instead, replace the word ``stable`` with ``nightly``,
   when adding the ``apt`` repository.

Install Envoy on CentOS Linux
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

You can `install Envoy on CentOS <https://www.getenvoy.io/install/envoy/centos/>`_
using `Get Envoy <https://www.getenvoy.io/>`__.

.. code-block:: console

   $ sudo yum install -y yum-utils
   $ sudo yum-config-manager --add-repo https://getenvoy.io/linux/centos/tetrate-getenvoy.repo
   $ sudo yum install -y getenvoy-envoy

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

   $ sudo yum install -y yum-utils
   $ sudo yum-config-manager --add-repo https://getenvoy.io/linux/rhel/tetrate-getenvoy.repo
   $ sudo yum install -y getenvoy-envoy

.. tip::

   You can enable/disable ``nightly`` using ``yum-config-manager``:

   .. code-block:: console

      $ sudo yum-config-manager --enable tetrate-getenvoy-nightly
      $ sudo yum-config-manager --disable tetrate-getenvoy-nightly

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
	 $ docker run envoyproxy/|envoy_docker_image| --version

      .. tip::

	 See the `envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy/tags/>`_ and
	 `envoyproxy/envoy-dev <https://hub.docker.com/r/envoyproxy/envoy-dev/tags/>`_
	 dockerhub pages for the list of available tags/versions.

	 :ref:`See here <install_binaries>` for a list of the available Envoy Docker image types.

   .. tab:: Get Envoy

      .. code-block:: console

	 $ docker pull getenvoy/envoy:stable
	 $ docker run getenvoy/envoy:stable --version

      .. tip::

	 To use the ``nightly`` version from `Get Envoy <https://www.getenvoy.io/>`__
	 replace the word ``stable`` with ``nightly`` in the above commands.
