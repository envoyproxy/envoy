.. _install_sandboxes_gzip:

Gzip
====

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

Enable compression in Envoy would save some bandwidth. Envoy support compression and decompression for both request and response,
not only for http protocol, but alsk https protocol.

Here only show how to enable response compression for http protocol with following cases:
- compression of files from an upstream server
- compression of Envoy's statistics

Step 1: Start all of our containers
***********************************

Change to the ``examples/gzip`` directory and bring up the docker composition.

.. code-block:: console

    $ pwd
    envoy/examples/gzip
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps
           Name                     Command               State                                                                    Ports
    -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    gzip_envoy-stats_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp,:::10000->10000/tcp, 0.0.0.0:10001->10001/tcp,:::10001->10001/tcp, 0.0.0.0:8089->8089/tcp,:::8089->8089/tcp
    gzip_service_1       python3 /code/service.py         Up

Step 2: Test Envoy's compression capabilities for upstream
**********************************************************

The sandbox is configured with two endpoints serving upstream files: ``/file.txt`` and ``/file.json``, only ``/file.json`` should be compressed.

First we use ``curl`` to check that the response from requesting file.json responds with the ``content-encoding: gzip`` header:

.. code-block:: console

    $ curl -siv -H "Accept-Encoding: gzip" localhost:8089/file.txt -o file.txt 2>&1 | grep "content-encoding"

As only files of type ``application/json`` are configured to be gzipped, requesting ``file.txt`` should not contain the ``content-encoding: gzip`` header.

.. code-block:: console

    $ curl -siv -H "Accept-Encoding: gzip" localhost:8089/file.json -o /dev/null 2>&1 | grep "content-encoding"
    < content-encoding: gzip

Step 3: Test Envoy's compression capabilities for Envoy's statistics
********************************************************************

The sandbox is configured with two ports serving Envoy's statistics: ``10000`` and ``10001``, only ``10001`` port should be compressed.

We still use same way to test:

.. code-block:: console

    $ curl -si -v -H "Accept-Encoding: gzip" localhost:10000/stats/prometheus 2>&1 | grep "content-encoding"

As expected, requesting ``10000`` port does not contain the ``content-encoding: gzip`` header.

.. code-block:: console

    $ curl -si -v -H "Accept-Encoding: gzip" localhost:10001/stats/prometheus 2>&1 | grep "content-encoding"
    < content-encoding: gzip
    Binary file (standard input) matches
