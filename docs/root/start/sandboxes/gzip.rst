.. _install_sandboxes_gzip:

Gzip
====

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

By enabling compression in Envoy you can save some network bandwidth, at the expense of increased processor usage.

Envoy supports compression and decompression for both requests and responses.

This sandbox provides examples of response compression and request decompression served over ``HTTP``. Although ``HTTPS`` is not demonstrated, compression can be used for this also.

The sandbox covers three scenarios:

- compression of files from an upstream server
- decompression of files from a downstream client
- compression of Envoy's own statistics

Step 1: Start all of our containers
***********************************

Change to the ``examples/gzip`` directory and bring up the docker composition.

.. code-block:: console

    $ pwd
    envoy/examples/gzip
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps
    Name                 Command                        State   Ports
    --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    gzip_envoy-stats_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp,:::10000->10000/tcp, 0.0.0.0:9901->9901/tcp,:::9901->9901/tcp, 0.0.0.0:9902->9902/tcp,:::9902->9902/tcp
    gzip_service_1       python3 /code/service.py       Up

Step 2: Test Envoy’s compression of upstream files
**************************************************

The sandbox is configured with two endpoints on port ``10000`` for serving upstream files:

- ``/file.txt``
- ``/file.json``

Only ``/file.json`` is configured to be compressed.

Use ``curl`` to check that the response from requesting ``file.json`` contains the ``content-encoding: gzip`` header.

You will need to add an ``accept-encoding: gzip`` request header.

.. code-block:: console

    $ curl -si -H "Accept-Encoding: gzip" localhost:10000/file.json | grep "content-encoding"
    content-encoding: gzip

As only files with a content-type of ``application/json`` are configured to be gzipped, the response from requesting ``file.txt`` should not contain the ``content-encoding: gzip`` header, and the file will not be compressed:

.. code-block:: console

    $ curl -si -H "Accept-Encoding: gzip" localhost:10000/file.txt | grep "content-encoding"

Step 3: Test Envoy’s decompression of downstream files
******************************************************

The sandbox is configured with an endpoint for uploading downstream files:

- ``/upload``

Use ``curl`` to get the compressed file ``file.gz``

.. code-block:: console

    $ curl -s -H "Accept-Encoding: gzip" -o file.gz localhost:10000/file.json

Use ``curl`` to check that the response from uploading ``file.gz`` contains the ``decompressed-size`` header.

You will need to add the ``content-encoding: gzip`` request header.

.. code-block:: console

    $ curl -si -H "Content-Encoding: gzip" localhost:10000/upload --data-binary "@file.gz" | grep "decompressed-size"
    decompressed-size: 10485760

Step 4: Test compression of Envoy’s statistics
**********************************************

The sandbox is configured with two ports serving Envoy’s admin and statistics interface:

- ``9901`` exposes the standard admin interface
- ``9902`` exposes a compressed version of the admin interface

Use ``curl`` to make a request for uncompressed statistics on port ``9901``, it should not contain the ``content-encoding`` header in the response:

.. code-block:: console

    $ curl -si -H "Accept-Encoding: gzip" localhost:9901/stats/prometheus | grep "content-encoding"

Now, use ``curl`` to make a request for the compressed statistics:

.. code-block:: console

    $ curl -si -H "Accept-Encoding: gzip" localhost:9902/stats/prometheus | grep "content-encoding"
    content-encoding: gzip

.. seealso::
    :ref:`Gzip Compression API <envoy_v3_api_msg_extensions.compression.gzip.compressor.v3.Gzip>`
        API and configuration reference for Envoy's gzip compression.

    :ref:`Gzip Decompression API <envoy_v3_api_msg_extensions.compression.gzip.decompressor.v3.Gzip>`
        API and configuration reference for Envoy's gzip decompression.

    :ref:`Compression configuration <config_http_filters_compressor>`
        Reference documentation for Envoy's compressor filter.

    :ref:`Decompression configuration <config_http_filters_decompressor>`
        Reference documentation for Envoy's decompressor filter.

    :ref:`Envoy admin quick start guide <start_quick_start_admin>`
        Quick start guide to the Envoy admin interface.
