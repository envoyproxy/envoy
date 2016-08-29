.. _install_sandboxes:

Sandboxes
=========

To get a flavor of what Envoy has to offer, we are releasing a
`docker compose <https://docs.docker.com/compose/>`_ sandbox that deploys a front
envoy and a couple of services (simple flask apps) colocated with a running
service envoy. The three containers will be deployed inside a virtual network
called ``envoymesh``.

Below you can see a graphic showing the docker compose deployment:

.. image:: /_static/docker_compose_v0.1.svg
  :width: 100%

All incoming requests are routed via the front envoy, which is acting as a reverse proxy sitting on
the edge of the ``envoymesh`` network. Port ``80`` is mapped to  port ``8000`` by docker compose
(see `docker-compose.yml
<https://github.com/lyft/envoy/blob/docker-example/example/docker-compose.yml>`_). Moreover, notice
that all  traffic routed by the front envoy to the service containers is actually routed to the
service envoys (routes setup in `front-envoy.json
<https://github.com/lyft/envoy/blob/docker-example/example/front-envoy.json>`_). In turn the service
envoys route the  request to the flask app via the loopback address (routes setup in
`service-envoy.json
<https://github.com/lyft/envoy/blob/docker-example/example/service-envoy.json>`_). This setup
illustrates the advantage of running service envoys  collocated with your services: all requests are
handled by the service envoy, and efficiently routed to your services.

Running the Sandbox
-------------------

The following documentation runs through the setup of an envoy cluster organized
as is described in the image above.

**Step 1: Install Docker**

Ensure that you have a recent versions of ``docker`, ``docker-compose`` and
``docker-machine`` installed.

A simple way to achieve this is via the `Docker Toolbox <https://www.docker.com/products/docker-toolbox>`_.

**Step 2: Docker Machine setup**

First let's create a new machine which will hold the containers::

    $ docker-machine create --driver virtualbox default
    $ eval $(docker-machine env default)

**Step 4: Start all of our containers**

::

    $ pwd
    /src/envoy/example
    $ docker-compose up --build -d
    $ docker-compose ps
            Name                       Command               State      Ports
    -------------------------------------------------------------------------------------------------------------
    example_service1_1      /bin/sh -c /usr/local/bin/ ...    Up       80/tcp
    example_service2_1      /bin/sh -c /usr/local/bin/ ...    Up       80/tcp
    example_front-envoy_1   /bin/sh -c /usr/local/bin/ ...    Up       0.0.0.0:8000->80/tcp, 0.0.0.0:8001->8001/tcp

**Step 5: Test Envoy's routing capabilities**

You can now send a request to both services via the front-envoy.

For service1::

    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:39:19 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact

For service2::

    $ curl -v $(docker-machine ip default):8000/service/2
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/2 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 2
    < server: envoy
    < date: Fri, 26 Aug 2016 19:39:23 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 2)! hostname: 92f4a3737bbc resolvedhostname: 172.19.0.2
    * Connection #0 to host 192.168.99.100 left intact

Notice that each request, while sent to the front envoy, was correctly routed
to the respective application.

**Step 6: Test Envoy's load balancing capabilities**

Now let's scale up our service1 nodes to demonstrate the clustering abilities
of envoy.::

    $ docker-compose scale service1=3
    Creating and starting example_service1_2 ... done
    Creating and starting example_service1_3 ... done

Now if we send a request to service1, the fron envoy will load balance the
request by doing a round robin of the three service1 machines::

    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:40:21 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: 85ac151715c6 resolvedhostname: 172.19.0.3
    * Connection #0 to host 192.168.99.100 left intact
    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:40:22 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: 20da22cfc955 resolvedhostname: 172.19.0.5
    * Connection #0 to host 192.168.99.100 left intact
    $ curl -v $(docker-machine ip default):8000/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2016 19:40:24 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact

**Step 7: enter containers and curl services**

In addition of using ``curl`` from your host machine, you can also enter the
containers themselves and ``curl`` from inside them. To enter a container you
can use ``docker-compose exec <container_name> /bin/bash``. For example we can
enter the ``front-envoy`` container, and ``curl`` for services locally::

  $ docker-compose exec front-envoy /bin/bash
  root@81288499f9d7:/# curl localhost:80/service/1
  Hello from behind Envoy (service 1)! hostname: 85ac151715c6 resolvedhostname: 172.19.0.3
  root@81288499f9d7:/# curl localhost:80/service/1
  Hello from behind Envoy (service 1)! hostname: 20da22cfc955 resolvedhostname: 172.19.0.5
  root@81288499f9d7:/# curl localhost:80/service/1
  Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
  root@81288499f9d7:/# curl localhost:80/service/2
  Hello from behind Envoy (service 2)! hostname: 92f4a3737bbc resolvedhostname: 172.19.0.2

** Step 8: enter containers and curl admin**

When envoy runs it also attaches an ``admin`` to your desired port. In the example
configs the admin is bound to port ``8001``. We can ``curl`` it to gain useful information.
For example you can ``curl`` ``/server_info`` to get information about the
envoy version you are running. Addionally you can ``curl`` ``/stats`` to get
statistics. For example inside ``frontenvoy`` we can get::

  $ docker-compose exec front-envoy /bin/bash
  root@e654c2c83277:/# curl localhost:8001/server_info
  envoy 10e00b/RELEASE live 142 142 0
  root@e654c2c83277:/# curl localhost:8001/stats
  cluster.service1.external.upstream_rq_200: 3
  cluster.service1.external.upstream_rq_2xx: 3
  cluster.service1.max_host_weight: 1
  cluster.service1.membership_change: 2
  cluster.service1.membership_total: 3
  cluster.service1.update_attempt: 30
  cluster.service1.update_failure: 0
  cluster.service1.update_success: 30
  cluster.service1.upstream_cx_active: 3
  cluster.service1.upstream_cx_close_header: 0
  cluster.service1.upstream_cx_connect_fail: 0
  cluster.service1.upstream_cx_connect_timeout: 0
  cluster.service1.upstream_cx_destroy: 0
  cluster.service1.upstream_cx_destroy_local: 0
  cluster.service1.upstream_cx_destroy_local_with_active_rq: 0
  cluster.service1.upstream_cx_destroy_remote: 0
  cluster.service1.upstream_cx_destroy_remote_with_active_rq: 0
  cluster.service1.upstream_cx_destroy_with_active_rq: 0
  cluster.service1.upstream_cx_http1_total: 0
  cluster.service1.upstream_cx_http2_total: 3
  cluster.service1.upstream_cx_max_requests: 0
  cluster.service1.upstream_cx_none_healthy: 0
  cluster.service1.upstream_cx_protocol_error: 0
  cluster.service1.upstream_cx_rx_bytes_buffered: 0
  cluster.service1.upstream_cx_rx_bytes_total: 798
  cluster.service1.upstream_cx_total: 3
  cluster.service1.upstream_cx_tx_bytes_buffered: 0
  cluster.service1.upstream_cx_tx_bytes_total: 502
  cluster.service1.upstream_rq_200: 3
  cluster.service1.upstream_rq_2xx: 3
  cluster.service1.upstream_rq_active: 0
  cluster.service1.upstream_rq_cancelled: 0
  cluster.service1.upstream_rq_lb_healthy_panic: 0
  cluster.service1.upstream_rq_pending_active: 0
  cluster.service1.upstream_rq_pending_failure_eject: 0
  cluster.service1.upstream_rq_pending_overflow: 0
  cluster.service1.upstream_rq_pending_total: 0
  cluster.service1.upstream_rq_per_try_timeout: 0
  cluster.service1.upstream_rq_retry: 0
  cluster.service1.upstream_rq_retry_overflow: 0
  cluster.service1.upstream_rq_retry_success: 0
  cluster.service1.upstream_rq_rx_reset: 0
  cluster.service1.upstream_rq_timeout: 0
  cluster.service1.upstream_rq_total: 3
  cluster.service1.upstream_rq_tx_reset: 0
  cluster.service2.external.upstream_rq_200: 1
  cluster.service2.external.upstream_rq_2xx: 1
  cluster.service2.max_host_weight: 1
  cluster.service2.membership_change: 1
  cluster.service2.membership_total: 1
  cluster.service2.update_attempt: 30
  cluster.service2.update_failure: 0
  cluster.service2.update_success: 30
  cluster.service2.upstream_cx_active: 1
  cluster.service2.upstream_cx_close_header: 0
  cluster.service2.upstream_cx_connect_fail: 0
  cluster.service2.upstream_cx_connect_timeout: 0
  cluster.service2.upstream_cx_destroy: 0
  cluster.service2.upstream_cx_destroy_local: 0
  cluster.service2.upstream_cx_destroy_local_with_active_rq: 0
  cluster.service2.upstream_cx_destroy_remote: 0
  cluster.service2.upstream_cx_destroy_remote_with_active_rq: 0
  cluster.service2.upstream_cx_destroy_with_active_rq: 0
  cluster.service2.upstream_cx_http1_total: 0
  cluster.service2.upstream_cx_http2_total: 1
  cluster.service2.upstream_cx_max_requests: 0
  cluster.service2.upstream_cx_none_healthy: 0
  cluster.service2.upstream_cx_protocol_error: 0
  cluster.service2.upstream_cx_rx_bytes_buffered: 0
  cluster.service2.upstream_cx_rx_bytes_total: 266
  cluster.service2.upstream_cx_total: 1
  cluster.service2.upstream_cx_tx_bytes_buffered: 0
  cluster.service2.upstream_cx_tx_bytes_total: 167
  cluster.service2.upstream_rq_200: 1
  cluster.service2.upstream_rq_2xx: 1
  cluster.service2.upstream_rq_active: 0
  cluster.service2.upstream_rq_cancelled: 0
  cluster.service2.upstream_rq_lb_healthy_panic: 0
  cluster.service2.upstream_rq_pending_active: 0
  cluster.service2.upstream_rq_pending_failure_eject: 0
  cluster.service2.upstream_rq_pending_overflow: 0
  cluster.service2.upstream_rq_pending_total: 0
  cluster.service2.upstream_rq_per_try_timeout: 0
  cluster.service2.upstream_rq_retry: 0
  cluster.service2.upstream_rq_retry_overflow: 0
  cluster.service2.upstream_rq_retry_success: 0
  cluster.service2.upstream_rq_rx_reset: 0
  cluster.service2.upstream_rq_timeout: 0
  cluster.service2.upstream_rq_total: 1
  cluster.service2.upstream_rq_tx_reset: 0
  downstream_cx_proxy_proto_error: 0
  filesystem.flushed_by_timer: 9
  filesystem.reopen_failed: 0
  filesystem.write_buffered: 3
  filesystem.write_completed: 2
  filesystem.write_total_buffered: 157
  http.admin.downstream_cx_active: 1
  http.admin.downstream_cx_destroy: 3
  http.admin.downstream_cx_destroy_active_rq: 0
  http.admin.downstream_cx_destroy_local: 0
  http.admin.downstream_cx_destroy_local_active_rq: 0
  http.admin.downstream_cx_destroy_remote: 3
  http.admin.downstream_cx_destroy_remote_active_rq: 0
  http.admin.downstream_cx_drain_close: 0
  http.admin.downstream_cx_http1_active: 1
  http.admin.downstream_cx_http1_total: 4
  http.admin.downstream_cx_http2_active: 0
  http.admin.downstream_cx_http2_total: 0
  http.admin.downstream_cx_idle_timeout: 0
  http.admin.downstream_cx_protocol_error: 0
  http.admin.downstream_cx_rx_bytes_buffered: 83
  http.admin.downstream_cx_rx_bytes_total: 344
  http.admin.downstream_cx_ssl_active: 0
  http.admin.downstream_cx_ssl_total: 0
  http.admin.downstream_cx_total: 4
  http.admin.downstream_cx_tx_bytes_buffered: 0
  http.admin.downstream_cx_tx_bytes_total: 7952
  http.admin.downstream_rq_2xx: 3
  http.admin.downstream_rq_3xx: 0
  http.admin.downstream_rq_4xx: 0
  http.admin.downstream_rq_5xx: 0
  http.admin.downstream_rq_active: 1
  http.admin.downstream_rq_http1_total: 4
  http.admin.downstream_rq_http2_total: 0
  http.admin.downstream_rq_non_relative_path: 0
  http.admin.downstream_rq_response_before_rq_complete: 0
  http.admin.downstream_rq_rx_reset: 0
  http.admin.downstream_rq_total: 4
  http.admin.downstream_rq_tx_reset: 0
  http.admin.failed_generate_uuid: 0
  http.ingress_http.downstream_cx_active: 0
  http.ingress_http.downstream_cx_destroy: 4
  http.ingress_http.downstream_cx_destroy_active_rq: 0
  http.ingress_http.downstream_cx_destroy_local: 0
  http.ingress_http.downstream_cx_destroy_local_active_rq: 0
  http.ingress_http.downstream_cx_destroy_remote: 4
  http.ingress_http.downstream_cx_destroy_remote_active_rq: 0
  http.ingress_http.downstream_cx_drain_close: 0
  http.ingress_http.downstream_cx_http1_active: 0
  http.ingress_http.downstream_cx_http1_total: 4
  http.ingress_http.downstream_cx_http2_active: 0
  http.ingress_http.downstream_cx_http2_total: 0
  http.ingress_http.downstream_cx_idle_timeout: 0
  http.ingress_http.downstream_cx_protocol_error: 0
  http.ingress_http.downstream_cx_rx_bytes_buffered: 0
  http.ingress_http.downstream_cx_rx_bytes_total: 328
  http.ingress_http.downstream_cx_ssl_active: 0
  http.ingress_http.downstream_cx_ssl_total: 0
  http.ingress_http.downstream_cx_total: 4
  http.ingress_http.downstream_cx_tx_bytes_buffered: 0
  http.ingress_http.downstream_cx_tx_bytes_total: 1160
  http.ingress_http.downstream_rq_2xx: 4
  http.ingress_http.downstream_rq_3xx: 0
  http.ingress_http.downstream_rq_4xx: 0
  http.ingress_http.downstream_rq_5xx: 0
  http.ingress_http.downstream_rq_active: 0
  http.ingress_http.downstream_rq_http1_total: 4
  http.ingress_http.downstream_rq_http2_total: 0
  http.ingress_http.downstream_rq_non_relative_path: 0
  http.ingress_http.downstream_rq_response_before_rq_complete: 0
  http.ingress_http.downstream_rq_rx_reset: 0
  http.ingress_http.downstream_rq_total: 4
  http.ingress_http.downstream_rq_tx_reset: 0
  http.ingress_http.failed_generate_uuid: 0
  http.ingress_http.no_route: 0
  http.ingress_http.rq_redirect: 0
  http.ingress_http.rq_total: 4
  http2.header_overflow: 0
  http2.headers_cb_no_stream: 0
  http2.rx_reset: 0
  http2.trailers: 0
  http2.tx_reset: 0
  listener.80.downstream_cx_active: 0
  listener.80.downstream_cx_destroy: 4
  listener.80.downstream_cx_total: 4
  listener.8001.downstream_cx_active: 1
  listener.8001.downstream_cx_destroy: 3
  listener.8001.downstream_cx_total: 4
  server.days_until_first_cert_expiring: 2147483647
  server.live: 1
  server.memory_allocated: 520272
  server.memory_heap_size: 1048576
  server.parent_connections: 0
  server.total_connections: 0
  server.uptime: 145
  server.version: 1105931
  server.watchdog_mega_miss: 0
  server.watchdog_miss: 0
  stats.overflow: 0

Notice that we can get the number of members of upstream clusters, number of requests
fulfilled by them, information about http ingress, and a plethora of other useful
stats.
