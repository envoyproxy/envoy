.. _install_sandboxes_zstd:

Zstd
======

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

By enabling compression in Envoy you can save some network bandwidth, at the expense of increased processor usage.

Envoy supports compression and decompression for both requests and responses.

This sandbox provides an example of response compression served over ``HTTPS``.

The sandbox covers two scenarios:

- compression of files from an upstream server
- compression of Envoy's own statistics

Step 1: Start all of our containers
***********************************

Change to the ``examples/zstd`` directory and bring up the docker composition.

.. code-block:: console

    $ pwd
    envoy/examples/zstd
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps
    Name                 Command                        State   Ports
    --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    zstd_envoy-stats_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp,:::10000->10000/tcp, 0.0.0.0:9901->9901/tcp,:::9901->9901/tcp, 0.0.0.0:9902->9902/tcp,:::9902->9902/tcp
    zstd_service_1       python3 /code/service.py       Up

Step 2: Test Envoy’s compression of upstream files
**************************************************

The sandbox is configured with two endpoints on port ``10000`` for serving upstream files:

- ``/file.txt``
- ``/file.json``

Only ``/file.json`` is configured to be compressed.

Use ``curl`` to check that the response from requesting ``file.json`` contains the ``content-encoding: zstd`` header.

You will need to add an ``accept-encoding: zstd`` request header.

.. code-block:: console

    $ curl -ski -H "Accept-Encoding: zstd" https://localhost:10000/file.json | grep "content-encoding"
    content-encoding: zstd

As only files with a content-type of ``application/json`` are configured to be compressed, the response from requesting ``file.txt`` should not contain the ``content-encoding: zstd`` header, and the file will not be compressed:

.. code-block:: console

    $ curl -ski -H "Accept-Encoding: zstd" https://localhost:10000/file.txt | grep "content-encoding"

Step 3: Test compression of Envoy’s statistics
**********************************************

The sandbox is configured with two ports serving Envoy’s admin and statistics interface:

- ``9901`` exposes the standard admin interface without tls
- ``9902`` exposes a compressed version of the admin interface with tls

Use ``curl`` to make a request for uncompressed statistics on port ``9901``, it should not contain the ``content-encoding`` header in the response:

.. code-block:: console

    $ curl -ski -H "Accept-Encoding: zstd" http://localhost:9901/stats/prometheus | grep "content-encoding"

Now, use ``curl`` to make a request for the compressed statistics:

.. code-block:: console

    $ curl -ski -H "Accept-Encoding: zstd" https://localhost:9902/stats/prometheus | grep "content-encoding"
    content-encoding: zstd

.. seealso::
   :ref:`Zstd API <envoy_v3_api_msg_extensions.compression.zstd.compressor.v3.Zstd>`
      API and configuration reference for Envoy's zstd compression.

   :ref:`Compression configuration <config_http_filters_compressor>`
      Reference documentation for Envoy's compressor filter.

   :ref:`Envoy admin quick start guide <start_quick_start_admin>`
      Quick start guide to the Envoy admin interface.
