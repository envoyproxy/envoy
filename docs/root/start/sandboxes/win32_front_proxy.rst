Windows based Front proxy
=========================

.. include:: ../../_include/windows_support_ended.rst

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

To get a flavor of what Envoy has to offer on Windows, we are releasing a
`docker compose <https://docs.docker.com/compose/>`_ sandbox that deploys a front Envoy and a
couple of services (simple Flask apps) colocated with a running service Envoy.

The three containers will be deployed inside a virtual network called ``envoymesh``.

Below you can see a graphic showing the docker compose deployment:

.. image:: /_static/docker_compose_front_proxy.svg
  :width: 100%

All incoming requests are routed via the front Envoy, which is acting as a reverse proxy sitting on
the edge of the ``envoymesh`` network. Port ``8080``, ``8443``, and ``8001`` are exposed by docker
compose (see :download:`docker-compose.yaml <_include/front-proxy/docker-compose.yaml>`) to handle
``HTTP``, ``HTTPS`` calls to the services and requests to ``/admin`` respectively.

Moreover, notice that all traffic routed by the front Envoy to the service containers is actually
routed to the service Envoys (routes setup in :download:`envoy.yaml <_include/front-proxy/envoy.yaml>`).

In turn the service Envoys route the request to the Flask app via the loopback
address (routes setup in :download:`service-envoy.yaml <_include/front-proxy/service-envoy.yaml>`). This
setup illustrates the advantage of running service Envoys collocated with your services: all
requests are handled by the service Envoy, and efficiently routed to your services.

Step 1: Start all of our containers
***********************************

Change to the ``examples/front-proxy`` directory.

.. code-block:: console

    PS> $PWD
    D:\envoy\examples\win32-front-proxy
    PS> docker-compose build --pull
    PS> docker-compose up -d
    PS> docker-compose ps
        Name                            Command               State                                         Ports
    ------------------------------------------------------------------------------------------------------------------------------------------------------------
    envoy-front-proxy_front-envoy_1   powershell.exe ./start_env ... Up      10000/tcp, 0.0.0.0:8003->8003/tcp, 0.0.0.0:8080->8080/tcp, 0.0.0.0:8443->8443/tcp
    envoy-front-proxy_service1_1      powershell.exe ./start_ser ... Up      10000/tcp
    envoy-front-proxy_service2_1      powershell.exe ./start_ser ... Up      10000/tcp

Step 2: Test Envoy's routing capabilities
*****************************************

You can now send a request to both services via the ``front-envoy``.

For ``service1``:

.. code-block:: console

    PS> curl -v localhost:8080/service/1
    *   Trying ::1...
    * TCP_NODELAY set
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.55.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Wed, 05 May 2021 05:55:55 GMT
    < x-envoy-upstream-service-time: 18
    <
    Hello from behind Envoy (service 1)! hostname: 8a45bba91d83 resolvedhostname: 172.30.97.237
    * Connection #0 to host localhost left intact

For ``service2``:

.. code-block:: console

    PS> curl -v localhost:8080/service/2
    *   Trying ::1...
    * TCP_NODELAY set
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /service/2 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.55.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 93
    < server: envoy
    < date: Wed, 05 May 2021 05:57:03 GMT
    < x-envoy-upstream-service-time: 14
    <
    Hello from behind Envoy (service 2)! hostname: 51e28eb3c8b8 resolvedhostname: 172.30.109.113
    * Connection #0 to host localhost left intact

Notice that each request, while sent to the front Envoy, was correctly routed to the respective
application.

We can also use ``HTTPS`` to call services behind the front Envoy. For example, calling ``service1``:

.. code-block:: console

    PS> curl https://localhost:8443/service/1 -k -v
    *   Trying ::1...
    * TCP_NODELAY set
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8443 (#0)
    * schannel: SSL/TLS connection with localhost port 8443 (step 1/3)
    * schannel: disabled server certificate revocation checks
    * schannel: verifyhost setting prevents Schannel from comparing the supplied target name with the subject names in server certificates.
    * schannel: sending initial handshake data: sending 171 bytes...
    * schannel: sent initial handshake data: sent 171 bytes
    * schannel: SSL/TLS connection with localhost port 8443 (step 2/3)
    * schannel: failed to receive handshake, need more data
    * schannel: SSL/TLS connection with localhost port 8443 (step 2/3)
    * schannel: encrypted data got 1081
    * schannel: encrypted data buffer: offset 1081 length 4096
    * schannel: sending next handshake data: sending 93 bytes...
    * schannel: SSL/TLS connection with localhost port 8443 (step 2/3)
    * schannel: encrypted data got 258
    * schannel: encrypted data buffer: offset 258 length 4096
    * schannel: SSL/TLS handshake complete
    * schannel: SSL/TLS connection with localhost port 8443 (step 3/3)
    * schannel: stored credential handle in session cache
    > GET /service/1 HTTP/1.1
    > Host: localhost:8443
    > User-Agent: curl/7.55.1
    > Accept: */*
    >
    * schannel: client wants to read 102400 bytes
    * schannel: encdata_buffer resized 103424
    * schannel: encrypted data buffer: offset 0 length 103424
    * schannel: encrypted data got 286
    * schannel: encrypted data buffer: offset 286 length 103424
    * schannel: decrypted data length: 257
    * schannel: decrypted data added: 257
    * schannel: decrypted data cached: offset 257 length 102400
    * schannel: encrypted data buffer: offset 0 length 103424
    * schannel: decrypted data buffer: offset 257 length 102400
    * schannel: schannel_recv cleanup
    * schannel: decrypted data returned 257
    * schannel: decrypted data buffer: offset 0 length 102400
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Wed, 05 May 2021 05:57:45 GMT
    < x-envoy-upstream-service-time: 3
    <
    Hello from behind Envoy (service 1)! hostname: 8a45bba91d83 resolvedhostname: 172.30.97.237
    * Connection #0 to host localhost left intact

Step 3: Test Envoy's load balancing capabilities
************************************************

Now let's scale up our ``service1`` nodes to demonstrate the load balancing abilities of Envoy:

.. code-block:: console

    PS> docker-compose scale service1=3
    Creating and starting example_service1_2 ... done
    Creating and starting example_service1_3 ... done

Now if we send a request to ``service1`` multiple times, the front Envoy will load balance the
requests by doing a round robin of the three ``service1`` machines:

.. code-block:: console

    PS> curl -v localhost:8080/service/1
    *   Trying ::1...
    * TCP_NODELAY set
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.55.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 93
    < server: envoy
    < date: Wed, 05 May 2021 05:58:40 GMT
    < x-envoy-upstream-service-time: 22
    <
    Hello from behind Envoy (service 1)! hostname: 8d2359ee21a8 resolvedhostname: 172.30.101.143
    * Connection #0 to host localhost left intact
    PS> curl -v localhost:8080/service/1
    *   Trying ::1...
    * TCP_NODELAY set
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.55.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 91
    < server: envoy
    < date: Wed, 05 May 2021 05:58:43 GMT
    < x-envoy-upstream-service-time: 11
    <
    Hello from behind Envoy (service 1)! hostname: 41e1141eebf4 resolvedhostname: 172.30.96.11
    * Connection #0 to host localhost left intact
    PS> curl -v localhost:8080/service/1
    *   Trying ::1...
    * TCP_NODELAY set
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.55.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Wed, 05 May 2021 05:58:44 GMT
    < x-envoy-upstream-service-time: 7
    <
    Hello from behind Envoy (service 1)! hostname: 8a45bba91d83 resolvedhostname: 172.30.97.237
    * Connection #0 to host localhost left intact

Step 4: Enter containers and curl services
******************************************

In addition of using ``curl`` from your host machine, you can also enter the
containers themselves and ``curl`` from inside them. To enter a container you
can use ``docker-compose exec <container_name> /bin/bash``. For example we can
enter the ``front-envoy`` container, and ``curl`` for services locally:

.. code-block:: console

    PS> docker-compose exec front-envoy powershell
    PS C:\> (curl -UseBasicParsing http://localhost:8080/service/1).Content
    Hello from behind Envoy (service 1)! hostname: 41e1141eebf4 resolvedhostname: 172.30.96.11

    PS C:\> (curl -UseBasicParsing http://localhost:8080/service/1).Content
    Hello from behind Envoy (service 1)! hostname: 8a45bba91d83 resolvedhostname: 172.30.97.237

    PS C:\> (curl -UseBasicParsing http://localhost:8080/service/1).Content
    Hello from behind Envoy (service 1)! hostname: 8d2359ee21a8 resolvedhostname: 172.30.101.143


Step 5: Enter container and curl admin interface
************************************************

When Envoy runs it also attaches an ``admin`` to your desired port.

In the example configs the admin listener is bound to port ``8001``.

We can ``curl`` it to gain useful information:

- :ref:`/server_info <operations_admin_interface_server_info>` provides information about the Envoy version you are running.
- :ref:`/stats <operations_admin_interface_stats>` provides statistics about the  Envoy server.

In the example we can enter the ``front-envoy`` container to query admin:

.. code-block:: console

    PS> docker-compose exec front-envoy powershell
    PS C:\> (curl http://localhost:8003/server_info -UseBasicParsing).Content

.. code-block:: json

  {
    "version": "093e2ffe046313242144d0431f1bb5cf18d82544/1.15.0-dev/Clean/RELEASE/BoringSSL",
    "state": "LIVE",
    "hot_restart_version": "11.104",
    "command_line_options": {
      "base_id": "0",
      "use_dynamic_base_id": false,
      "base_id_path": "",
      "concurrency": 8,
      "config_path": "/etc/front-envoy.yaml",
      "config_yaml": "",
      "allow_unknown_static_fields": false,
      "reject_unknown_dynamic_fields": false,
      "ignore_unknown_dynamic_fields": false,
      "admin_address_path": "",
      "local_address_ip_version": "v4",
      "log_level": "info",
      "component_log_level": "",
      "log_format": "[%Y-%m-%d %T.%e][%t][%l][%n] [%g:%#] %v",
      "log_format_escaped": false,
      "log_path": "",
      "service_cluster": "front-proxy",
      "service_node": "",
      "service_zone": "",
      "drain_strategy": "Gradual",
      "mode": "Serve",
      "disable_hot_restart": false,
      "enable_mutex_tracing": false,
      "restart_epoch": 0,
      "cpuset_threads": false,
      "disabled_extensions": [],
      "bootstrap_version": 0,
      "hidden_envoy_deprecated_max_stats": "0",
      "hidden_envoy_deprecated_max_obj_name_len": "0",
      "file_flush_interval": "10s",
      "drain_time": "600s",
      "parent_shutdown_time": "900s"
    },
    "uptime_current_epoch": "188s",
    "uptime_all_epochs": "188s"
  }

.. code-block:: console

    PS C:\> (curl http://localhost:8003/stats -UseBasicParsing).Content
    cluster.service1.external.upstream_rq_200: 7
    ...
    cluster.service1.membership_change: 2
    cluster.service1.membership_total: 3
    ...
    cluster.service1.upstream_cx_http2_total: 3
    ...
    cluster.service1.upstream_rq_total: 7
    ...
    cluster.service2.external.upstream_rq_200: 2
    ...
    cluster.service2.membership_change: 1
    cluster.service2.membership_total: 1
    ...
    cluster.service2.upstream_cx_http2_total: 1
    ...
    cluster.service2.upstream_rq_total: 2
    ...

Notice that we can get the number of members of upstream clusters, number of requests fulfilled by
them, information about http ingress, and a plethora of other useful stats.

.. seealso::

   :ref:`Envoy admin quick start guide <start_quick_start_admin>`
      Quick start guide to the Envoy admin interface.
