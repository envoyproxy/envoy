.. _install_sandboxes_load_reporting_service:

Load Reporting Service (LRS)
============================

This simple example demonstrates Envoy's Load Reporting Service (LRS) capability and how to use it

All incoming requests are routed via Envoy to a simple goLang web server aka http_server.
Envoy is configured to initiate the connection with LRS Server. LRS Server enables the stats by sending LoadStatsResponse.
Sending requests to http_server will be counted towards successful requests and will be visible in LRS Server logs.


Running the Sandbox
~~~~~~~~~~~~~~~~~~~

The following documentation runs through the setup of an Envoy cluster organized
as is described in the image above.

**Step 1: Install Docker**

Ensure that you have a recent version of ``docker`` and ``docker-compose`` installed.

A simple way to achieve this is via the `Docker Desktop <https://www.docker.com/products/docker-desktop>`_.

**Step 2: Clone the Envoy repo, and start all of our containers**

If you have not cloned the Envoy repo, clone it with ``git clone git@github.com:envoyproxy/envoy``
or ``git clone https://github.com/envoyproxy/envoy.git``

Terminal 1

.. code-block:: console

    $ pwd
    envoy/examples/load_reporting_service
    $ docker-compose pull
    $ docker-compose up


Terminal 2

.. code-block:: console

    $ pwd
    envoy/examples/load_reporting_service
    $ docker-compose ps

            Name                         Command             State                            Ports
    --------------------------------------------------------------------------------------------------------------------------------------
    load_reporting_service_http_service_1   /bin/sh -c /usr/local/bin/ ...   Up      10000/tcp, 0.0.0.0:80->80/tcp, 0.0.0.0:8081->8081/tcp
    load_reporting_service_lrs_server_1     go run main.go                   Up      0.0.0.0:18000->18000/tcp

**Step 3: Start sending stream of HTTP requests**

Terminal 2

.. code-block:: console

  $ pwd
  envoy/examples/load_reporting_service
  $ docker-compose exec http_service bash
  $ bash code/send_requests.sh

The script above (``send_requests.sh``) sends a continuous stream of HTTP requests to Envoy, which in turn forwards the requests to the backend service.

**Step 4: See Envoy Stats**

In Terminal 1 you should see

Terminal 1

.. code-block:: console
    lrs_server_1    | 2020/02/02 23:32:55 LRS Server is up and running on :18000
    lrs_server_1    | 2020/02/02 23:32:59 Adding new cluster to cache `http_service`
    lrs_server_1    | 2020/02/02 23:33:05 Creating LRS response with frequency - 7 secs
    http_service_1  | 127.0.0.1 - - [02/Feb/2020 23:33:22] "GET /service HTTP/1.1" 200 -
    http_service_1  | 127.0.0.1 - - [02/Feb/2020 23:33:23] "GET /service HTTP/1.1" 200 -
    http_service_1  | 127.0.0.1 - - [02/Feb/2020 23:33:24] "GET /service HTTP/1.1" 200 -
    http_service_1  | 127.0.0.1 - - [02/Feb/2020 23:33:25] "GET /service HTTP/1.1" 200 -
    http_service_1  | 127.0.0.1 - - [02/Feb/2020 23:33:26] "GET /service HTTP/1.1" 200 -
    lrs_server_1    | 2020/02/02 23:33:26 Got stats from cluster `http_service` - cluster_name:"local_service" upstream_locality_stats:<locality:<> total_successful_requests:5 total_issued_requests:5 > load_report_interval:<seconds:6 nanos:993629000 >
