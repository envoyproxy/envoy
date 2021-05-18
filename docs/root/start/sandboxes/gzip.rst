.. _install_sandboxes_gzip:

Gzip
====

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

Enable compression in Envoy would save some bandwidth.

Step 1: Start all of our containers
***********************************

Change to the ``examples/gzip`` directory.

.. code-block:: console

    $ pwd
    envoy/examples/gzip
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps
           Name                     Command               State                                                                    Ports
    -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
    gzip_envoy-stats_1   /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:8001->8001/tcp,:::8001->8001/tcp, 0.0.0.0:8002->8002/tcp,:::8002->8002/tcp, 0.0.0.0:8089->8089/tcp,:::8089->8089/tcp
    gzip_service_1       python3 /code/service.py         Up

Step 2: Test Envoy's compression capabilities for upstream
**********************************************************

Here we have two endpoints: ``/file.txt`` and ``/file.json``, only ``/file.json`` should be compressed.

Let us try it:

.. code-block:: console

    $ curl -si -H "Accept-Encoding: gzip" localhost:8089/file.txt -o file.txt
    $ curl -si -H "Accept-Encoding: gzip" localhost:8089/file.json -o file.json
    $ ls -sh file.txt
    11M file.txt
    $ $ ls -sh file.json
    12K file.json

The size of `file.txt` is not 10M since `ls` rounds up.

Step 3: View Envoy's stats capabilities
***************************************

You can now send a request to get prometheus stat.

For original port:

.. code-block:: console

    $ curl localhost:8001/stats/prometheus | tail -n 10
    envoy_server_initialization_time_ms_bucket{le="30000"} 1
    envoy_server_initialization_time_ms_bucket{le="60000"} 1
    envoy_server_initialization_time_ms_bucket{le="300000"} 1
    envoy_server_initialization_time_ms_bucket{le="600000"} 1
    envoy_server_initialization_time_ms_bucket{le="1800000"} 1
    envoy_server_initialization_time_ms_bucket{le="3600000"} 1
    envoy_server_initialization_time_ms_bucket{le="+Inf"} 1
    envoy_server_initialization_time_ms_sum{} 9.0500000000000007105427357601002
    envoy_server_initialization_time_ms_count{} 1

For listen port:

.. code-block:: console

    $ curl localhost:8002/stats/prometheus | tail -n 10
    envoy_server_initialization_time_ms_bucket{le="30000"} 1
    envoy_server_initialization_time_ms_bucket{le="60000"} 1
    envoy_server_initialization_time_ms_bucket{le="300000"} 1
    envoy_server_initialization_time_ms_bucket{le="600000"} 1
    envoy_server_initialization_time_ms_bucket{le="1800000"} 1
    envoy_server_initialization_time_ms_bucket{le="3600000"} 1
    envoy_server_initialization_time_ms_bucket{le="+Inf"} 1
    envoy_server_initialization_time_ms_sum{} 9.0500000000000007105427357601002
    envoy_server_initialization_time_ms_count{} 1

Step 4: Test Envoy's compression capabilities for Envoy's stats
***************************************************************

Now let's demonstrate the compression abilities of Envoy:

For original port:

.. code-block:: console

    $ curl -si -v -H "Accept-Encoding: gzip" localhost:8001/stats/prometheus -o /dev/null 2>&1 | grep -E "content-encoding|data"
    { [43231 bytes data]

    $ curl --head -H "Accept-Encoding: gzip" localhost:8001/stats/prometheus
    HTTP/1.1 200 OK
    content-type: text/plain; charset=UTF-8
    cache-control: no-cache, max-age=0
    x-content-type-options: nosniff
    date: Tue, 18 May 2021 06:09:36 GMT
    server: envoy
    transfer-encoding: chunked

For listen port:

.. code-block:: console

    $ curl -si -v -H "Accept-Encoding: gzip" localhost:8002/stats/prometheus -o /dev/null 2>&1 | grep -E "content-encoding|data"
    < content-encoding: gzip
    { [7977 bytes data]

    $ curl --head -H "Accept-Encoding: gzip" localhost:8002/stats/prometheus
    HTTP/1.1 200 OK
    content-type: text/plain; charset=UTF-8
    cache-control: no-cache, max-age=0
    x-content-type-options: nosniff
    date: Tue, 18 May 2021 06:09:38 GMT
    server: envoy
    x-envoy-upstream-service-time: 8
    vary: Accept-Encoding
    transfer-encoding: chunked
