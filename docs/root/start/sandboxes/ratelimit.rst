.. _install_sandboxes_ratelimit:

Local Ratelimit
===============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

Rate limiting is used to control the rate of requests sent or received by a network interface controller, which is helpful to prevent DoS attacks and limit web scraping.

Envoy supports both local (non-distributed) and global rate limiting, and two types for local rate limiting:
- L4 connections via the :ref:`local rate limit filter <config_network_filters_local_rate_limit>`
- HTTP requests via the :ref:`HTTP local rate limit filter <config_http_filters_local_rate_limit>`

This sandbox provides an example of rate limiting of L4 connections.

Step 1: Start all of our containers
***********************************

Change to the ``examples/ratelimit`` directory and bring up the docker composition.

.. code-block:: console

    $ pwd
    envoy/examples/ratelimit
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps
    Name                        Command                          State   Ports
    -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    ratelimtit_envoy-stat_1     /docker-entrypoint.sh /usr ...   Up      0.0.0.0:10000->10000/tcp,:::10000->10000/tcp, 0.0.0.0:9901->9901/tcp,:::9901->9901/tcp, 0.0.0.0:9902->9902/tcp,:::9902->9902/tcp
    ratelimtit_service_1        /docker-entrypoint.sh ngin ...   Up      80/tcp

Step 2: Test rate limiting of upstream service
**********************************************

The sandbox is configured with `10000` port for upstream service.

Now, use ``curl`` to make a request four times for the limited upsteam service:

.. code-block:: console

    $ for i in {1..4}; do curl -si localhost:10000 | grep "x-local-rate-limit"; done
    x-local-rate-limit: true
    x-local-rate-limit: true

The second and third request are limited as configured, which is expected.

Step 3: Test rate limiting of Envoy’s statistics
************************************************

The sandbox is configured with two ports serving Envoy’s admin and statistics interface:

- ``9901`` exposes the standard admin interface
- ``9902`` exposes a rate limitied version of the admin interface

Use ``curl`` to make a request four times for unlimited statistics on port ``9901``, it should not contain the ``x-local-rate-limit`` header in the response:

.. code-block:: console

    $ for i in {1..4}; do curl -si localhost:9901/stats/prometheus | grep "x-local-rate-limit"; done

Now, use ``curl`` to make a request four times for the limited statistics:

.. code-block:: console

    $ for i in {1..4}; do curl -si localhost:9902/stats/prometheus | grep "x-local-rate-limit"; done
    x-local-rate-limit: true
    x-local-rate-limit: true

The second and third request are limited as configured, which is expected.

.. seealso::
   :ref:`global rate limiting <arch_overview_global_rate_limit>`
      Reference documentation for Envoy's gloval rate limiting.
