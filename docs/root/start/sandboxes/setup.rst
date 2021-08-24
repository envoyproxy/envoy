.. _start_sandboxes_setup:

Setup the sandbox environment
=============================

Before you can run the Envoy sandboxes you will need to set up your environment
with :ref:`Docker <start_sandboxes_setup_docker>` and
:ref:`Docker Compose <start_sandboxes_setup_docker_compose>`.

You should also clone the :ref:`Envoy repository <start_sandboxes_setup_envoy>` with
:ref:`Git <start_sandboxes_setup_git>`

Some of the examples require the installation of
:ref:`additional dependencies <start_sandboxes_setup_additional>`.

It is indicated in the sandbox documentation where this is the case.

.. tip::

   If you are working on a Mac OS or Windows system, a simple way to install both
   :ref:`Docker <start_sandboxes_setup_docker>` and
   :ref:`Docker Compose <start_sandboxes_setup_docker_compose>` is with
   `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

.. _start_sandboxes_setup_docker:

Install Docker
--------------

Ensure that you have a recent versions of ``docker`` installed.

You will need a minimum version of ``18.06.0+``.

Version ``19.03`` is well tested.

The user account running the examples will need to have permission to use Docker on your system.

Full instructions for installing Docker can be found on the `Docker website <https://docs.docker.com/get-docker/>`_

If you want to use the Windows based Envoy images make sure that you
`switch Docker to use Windows containers <https://docs.docker.com/docker-for-windows/#switch-between-windows-and-linux-containers>`_.

.. _start_sandboxes_setup_docker_compose:

Install Docker Compose
----------------------

The examples use
`Docker compose configuration version 3.7 <https://docs.docker.com/compose/compose-file/compose-versioning/#version-37>`_.

You will need to install a fairly recent version of `Docker Compose <https://docs.docker.com/compose/>`_.

Version ``1.27.4`` is well tested.

Docker Compose is a `python application <https://pypi.org/project/docker-compose/>`_ and can be
installed through a variety of methods including `pip <https://pip.pypa.io/en/stable/>`_ and
`native operating system installation <https://docs.docker.com/compose/install/>`_.

.. _start_sandboxes_setup_git:

Install Git
-----------

The Envoy project is managed using `Git <https://git-scm.com/>`_.

You can `find instructions for installing Git on various operating systems here <https://git-scm.com/book/en/v2/Getting-Started-Installing-Git>`_.

.. _start_sandboxes_setup_envoy:

Clone the Envoy repository
--------------------------

If you have not cloned the `Envoy repository <https://github.com/envoyproxy/envoy>`_ already,
clone it with:

.. tabs::

   .. code-tab:: console SSH

      git clone git@github.com:envoyproxy/envoy

   .. code-tab:: console HTTPS

      git clone https://github.com/envoyproxy/envoy.git

.. _start_sandboxes_setup_additional:

Additional dependencies
-----------------------

The following utilities are used in only some of the sandbox examples, and installation is
therefore optional.

.. _start_sandboxes_setup_curl:

curl
~~~~

Many of the examples use the `curl <https://curl.se/>`_ utility to make ``HTTP`` requests.

Instructions for installing `curl <https://curl.se/>`_ on many platforms and operating systems
can be `found on the curl website <https://curl.haxx.se/download.html>`_.

.. _start_sandboxes_setup_jq:

jq
~~~

The `jq <https://stedolan.github.io/jq/>`_ tool is very useful for parsing ``json`` data,
whether it be ``HTTP`` response data, logs or statistics.

Instructions for installing `jq <https://stedolan.github.io/jq/>`_ on many platforms and operating systems
can be `found on the jq website <https://stedolan.github.io/jq/download/>`_.

.. _start_sandboxes_setup_netcat:

netcat
~~~~~~

Binary distributions of `Netcat <https://nmap.org/ncat/>`_ are available for Mac OS with `brew <https://brew.sh>`_
and in most flavours of Linux.

Ncat is integrated with Nmap and is available in the standard Nmap download packages (including source code and Linux, Windows, and Mac binaries) available from the `Nmap download page <http://nmap.org/download.html>`_.


.. _start_sandboxes_setup_openssl:

openssl
~~~~~~~

`OpenSSL <https://www.openssl.org/>`_ is a robust, commercial-grade, and full-featured toolkit for
the Transport Layer Security (``TLS``) and Secure Sockets Layer (``SSL``) protocols.

Binary distributions of `OpenSSL <https://www.openssl.org/>`_ are available for Mac OS with `brew <https://brew.sh>`_
and in most if not all flavours of Linux.

Windows users can either use an `unofficial binary <https://wiki.openssl.org/index.php/Binaries>`_ or compile from source.

Check for installation instructions specific to your operating system.

.. _start_sandboxes_setup_redis:

redis
~~~~~

Binary distributions of `Redis <https://redis.io/>`_ are available for Mac OS with `brew <https://brew.sh>`_
and in most flavours of Linux.

Windows users should check out the `Windows port of Redis <https://github.com/microsoftarchive/redis>`_.

Check for installation instructions specific to your operating system.
