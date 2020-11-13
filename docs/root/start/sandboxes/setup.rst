.. _start_sandboxes_setup:

Setup the sandbox environment
=============================

.. _start_sandboxes_setup_docker:

Install Docker
--------------

Ensure that you have a recent versions of ``docker`` and ``docker-compose`` installed.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

The user account running the examples will need to have permission to use Docker on your system.

.. _start_sandboxes_setup_docker_compose:

Install Docker Compose
----------------------

You will need a fairly recent version of docker-compose.

Most of the examples use version ``3.7`` of the docker-compose configuration, so your version
should be able to parse this configuration.

.. _start_sandboxes_setup_git:

Install Git
-----------

get git...

.. _start_sandboxes_setup_envoy:

Clone the Envoy repo
--------------------

If you have not cloned the Envoy repo already, clone it with:

.. tabs::

   .. code-tab:: console SSH

      git clone git@github.com:envoyproxy/envoy

   .. code-tab:: console HTTPS

      git clone https://github.com/envoyproxy/envoy.git

.. _start_sandboxes_setup_additional:

Additional dependencies
-----------------------

The following utilities are used in only some of the sandbox examples.

.. _start_sandboxes_setup_curl:

curl
~~~~

Some of the examples use the `curl <https://curl.se/>`_ utility to make ``HTTP`` requests.

.. _start_sandboxes_setup_jq:

jq
~~~

`jq <https://stedolan.github.io/jq/>`_

.. _start_sandboxes_setup_openssl:

openssl
~~~~~~~

`openssl <https://www.openssl.org/>`_

.. _start_sandboxes_setup_redis:

redis
~~~~~

`redis <https://redis.io/>`_
