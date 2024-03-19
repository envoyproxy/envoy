.. _install_sandboxes_cache_mirror:

Caching mirror
==============

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

This is a simple example to demonstrate caching resources from an upstream, validating their contents against known checksums using the ``ChecksumFilter``.

This can be useful, for example, to cache build assets for improved speed or reliability.

The example uses a backend that serves 2 endpoints ``/foo`` and ``/bar``, that are expected to return ``FOO`` and ``BAR`` respectively.

In the latter case it does not and therefore fails validation and is not cached or proxied.

The cache mirror inserts a cache-forever header in any validated resources to ensure they are cached without expiry. (TODO)

Step 1: Start all of our containers
***********************************

Change to the ``examples/cache-mirror`` directory.

.. code-block:: console

    $ pwd
    envoy/examples/cache-mirror
    $ docker compose pull
    $ docker compose up --build -d
    $ docker compose ps

    NAME                         IMAGE                      COMMAND                 SERVICE       CREATED        STATUS        PORTS
    ------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    cache-mirror-backend-1       cache-mirror-backend       "/docker-entrypoint.…"  backend       6 seconds ago  Up 5 seconds  10000/tcp
    cache-mirror-mirror-proxy-1  cache-mirror-mirror-proxy  "/docker-entrypoint.…"  mirror-proxy  6 seconds ago  Up 5 seconds  0.0.0.0:10000->10000/tcp, :::10000->10000/tcp


Step 2: Fetch the cached asset
******************************

Step 3: Check the stats
***********************

Step 4: Fetch the cached asset again
************************************

Step 5: Confirm the asset was served from cache
***********************************************

Step 6: Fetch cached asset with invalid checksum
************************************************

Step 7: Confirm the asset was not cached
****************************************


.. seealso::

   :ref:`Envoy Cache filter configuration <config_http_filters_cache>`
      Learn more about configuring the Envoy Cache filter.
