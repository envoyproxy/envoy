.. _install_sandboxes_front_proxy:

前端代理
===========

为了帮助大家了解如何使用 Envoy 作为前端代理，我们发布了一个 `docker compose <https://docs.docker.com/compose/>`_ 沙盒，该沙盒中部署了一个前端 Envoy 和几个后端服务，每个后端由 Envoy 和应用（简单的 Flask 应用）合并部署在一起组成。这三个容器将被部署在名为 ``envoymesh`` 的虚拟网络中。

下面是使用 docker compose 部署的架构图：

.. image:: /_static/docker_compose_front_proxy.svg
  :width: 100%

所有的入向请求都通过前端 Envoy 进行路由，该 Envoy 相当于 ``envoymesh`` 网络边缘的反向代理。docker compose（参阅：:repo:`/examples/front-proxy/docker-compose.yaml`）暴露了 ``8080``，``8443`` 端口来接受 ``HTTP``，``HTTPS`` 请求，并根据路径分别将它们路由到对应的服务上，以及通过 ``8001`` 端口来接受 Envoy 自带的 ``admin`` 服务。

此外请注意，所有流量实际上从前端 Envoy 路由到服务容器的 Envoy 上（参阅 :repo:`/examples/front-proxy/front-envoy.yaml` 中的路由设置）。

然后，Envoy 通过 loopback 地址（参阅 :repo:`/examples/front-proxy/service-envoy.yaml` 中的路由设置）将请求路由到 Flask 应用程序。这种设置演示了将 Envoy 与你的服务合并部署的优势：所有请求都由 Envoy 处理，并有效地路由到你的服务。

运行沙盒
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3：启动所有容器
***********************************

.. code-block:: console

    $ pwd
    envoy/examples/front-proxy
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps

              Name                         Command               State                                         Ports
    ------------------------------------------------------------------------------------------------------------------------------------------------------
    front-proxy_front-envoy_1   /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8080->8080/tcp, 0.0.0.0:8001->8001/tcp, 0.0.0.0:8443->8443/tcp
    front-proxy_service1_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
    front-proxy_service2_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp

步骤 4：测试 Envoy 的路由能力
*****************************************

你现在可以通过 ``front-envoy`` 向两个服务发送请求。

向 ``service1`` 发请求：

.. code-block:: console

    $ curl -v localhost:8080/service/1
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Mon, 06 Jul 2020 06:20:00 GMT
    < x-envoy-upstream-service-time: 2
    <
    Hello from behind Envoy (service 1)! hostname: 36418bc3c824 resolvedhostname: 192.168.160.4

向 ``service2`` 发请求：

.. code-block:: console

    $ curl -v localhost:8080/service/2
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8080 (#0)
    > GET /service/2 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Mon, 06 Jul 2020 06:23:13 GMT
    < x-envoy-upstream-service-time: 2
    <
    Hello from behind Envoy (service 2)! hostname: ea6165ee4fee resolvedhostname: 192.168.160.2

能看到，每个请求在发送给前端 Envoy 后被正确路由到相应的应用程序。

我们也可以通过 ``HTTPS`` 请求前端 Envoy 后的服务。例如，向 ``service1``：

.. code-block:: console

    $ curl https://localhost:8443/service/1 -k -v
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8443 (#0)
    * ALPN, offering h2
    * ALPN, offering http/1.1
    * successfully set certificate verify locations:
    *   CAfile: /etc/ssl/cert.pem
      CApath: none
    * TLSv1.2 (OUT), TLS handshake, Client hello (1):
    * TLSv1.2 (IN), TLS handshake, Server hello (2):
    * TLSv1.2 (IN), TLS handshake, Certificate (11):
    * TLSv1.2 (IN), TLS handshake, Server key exchange (12):
    * TLSv1.2 (IN), TLS handshake, Server finished (14):
    * TLSv1.2 (OUT), TLS handshake, Client key exchange (16):
    * TLSv1.2 (OUT), TLS change cipher, Change cipher spec (1):
    * TLSv1.2 (OUT), TLS handshake, Finished (20):
    * TLSv1.2 (IN), TLS change cipher, Change cipher spec (1):
    * TLSv1.2 (IN), TLS handshake, Finished (20):
    * SSL connection using TLSv1.2 / ECDHE-RSA-CHACHA20-POLY1305
    * ALPN, server did not agree to a protocol
    * Server certificate:
    *  subject: CN=front-envoy
    *  start date: Jul  5 15:18:44 2020 GMT
    *  expire date: Jul  5 15:18:44 2021 GMT
    *  issuer: CN=front-envoy
    *  SSL certificate verify result: self signed certificate (18), continuing anyway.
    > GET /service/1 HTTP/1.1
    > Host: localhost:8443
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Mon, 06 Jul 2020 06:17:14 GMT
    < x-envoy-upstream-service-time: 3
    <
    Hello from behind Envoy (service 1)! hostname: 36418bc3c824 resolvedhostname: 192.168.160.4

步骤 5：测试 Envoy 的负载均衡能力
************************************************

现在增加 ``service1`` 的节点数量来演示 Envoy 的负载均衡能力：

.. code-block:: console

    $ docker-compose scale service1=3
    Creating and starting example_service1_2 ... done
    Creating and starting example_service1_3 ... done

现在，如果我们多次向 ``service1`` 发送请求，前端 Envoy 将通过 round-robin 轮询三台 ``service1`` 机器来实现负载均衡：

.. code-block:: console

    $ curl -v localhost:8080/service/1
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: localhost:8080
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 92
    < server: envoy
    < date: Mon, 06 Jul 2020 06:21:47 GMT
    < x-envoy-upstream-service-time: 6
    <
    Hello from behind Envoy (service 1)! hostname: 3dc787578c23 resolvedhostname: 192.168.160.6

    $ curl -v localhost:8080/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8080
    > User-Agent: curl/7.54.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2018 19:40:22 GMT
    <
    Hello from behind Envoy (service 1)! hostname: 3a93ece62129 resolvedhostname: 192.168.160.5

    $ curl -v localhost:8080/service/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8080 (#0)
    > GET /service/1 HTTP/1.1
    > Host: 192.168.99.100:8080
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2018 19:40:24 GMT
    < x-envoy-protocol-version: HTTP/1.1
    <
    Hello from behind Envoy (service 1)! hostname: 36418bc3c824 resolvedhostname: 192.168.160.4

步骤 6：进入容器并 curl 服务
******************************************

除了使用主机上的 ``curl`` 外，你还可以进入容器并从容器里面 ``curl``。要进入容器，可以使用 ``docker-compose exec <container_name> /bin/bash`` 命令。例如，我们可以进入 ``front-envoy`` 容器，并在本地 ``curl`` 服务：

.. code-block:: console

    $ docker-compose exec front-envoy /bin/bash
    root@81288499f9d7:/# curl localhost:8080/service/1
    Hello from behind Envoy (service 1)! hostname: 85ac151715c6 resolvedhostname: 172.19.0.3
    root@81288499f9d7:/# curl localhost:8080/service/1
    Hello from behind Envoy (service 1)! hostname: 20da22cfc955 resolvedhostname: 172.19.0.5
    root@81288499f9d7:/# curl localhost:8080/service/1
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    root@81288499f9d7:/# curl localhost:8080/service/2
    Hello from behind Envoy (service 2)! hostname: 92f4a3737bbc resolvedhostname: 172.19.0.2

步骤7：进入容器并 curl admin
**************************************

当 Envoy 启动时，也会同时启动一个 ``admin`` 服务并绑定指定的端口。

在示例配置中 ``admin`` 绑定到了 ``8001`` 端口。

我们可以通过 ``curl`` 它获得有用的信息：

- ``/server_info`` 提供正在运行的 Envoy 版本的信息。
- ``/stats`` 提供 Envoy 服务相关的统计数据。

在下面的例子里，我们进入 ``front-envoy`` 容器并查询 ``admin`` 服务：

.. code-block:: console

    $ docker-compose exec front-envoy /bin/bash
    root@e654c2c83277:/# curl localhost:8001/server_info

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

    root@e654c2c83277:/# curl localhost:8001/stats
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

能看到，我们可以获取上游集群的成员数量，它们完成的请求数量，有关 http 入口的信息以及大量其他有用的统计数据。
