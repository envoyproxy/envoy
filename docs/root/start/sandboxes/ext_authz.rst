.. _install_sandboxes_ext_authz:

外部授权过滤器
=============================

外部授权沙箱演示了 Envoy 的 :ref:`ext_authz 过滤器 <config_http_filters_ext_authz>` 功能，该功能可将通过 Envoy 传入请求的授权委派给外部服务。

尽管 ext_authz 也可以用作网络过滤器，但此沙箱仅限于展示 ext_authz HTTP 过滤器，该过滤器支持调用 HTTP 或 gRPC 服务。

此沙箱的设置与 front-proxy 部署非常相似，但是对代理后面上游服务的调用会被外部 HTTP 或 gRPC 服务检查。在此沙箱中，对于每个授权的调用，外部授权服务都会在原始请求头部添加 ``x-current-user``，然后该调用会被转发到上游服务。

运行沙箱
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3： 启动所有容器
***********************************

要构建此沙箱示例并启动示例服务，请运行以下命令：

.. code-block:: console

    $ pwd
    envoy/examples/ext_authz
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

                   Name                             Command               State                             Ports
    ---------------------------------------------------------------------------------------------------------------------------------------
    ext_authz_ext_authz-grpc-service_1   /app/server -users /etc/us       Up
    ext_authz_ext_authz-http-service_1   docker-entrypoint.sh node        Up
    ext_authz_front-envoy_1              /docker-entrypoint.sh /bin       Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
    ext_authz_upstream-service_1         python3 /app/service/server.py   Up

.. note::

    此沙箱具有受 ``FRONT_ENVOY_YAML`` 环境变量控制的多个设置，这些设置指向要使用的有效 Envoy 配置。``FRONT_ENVOY_YAML`` 可以在 ``.env`` 文件中定义默认值，也可以在运行 ``docker-compose up`` 命令时内联提供。更多有关信息，请参阅 `Compose 文档中的环境变量 <https://docs.docker.com/compose/environment-variables>`_。

默认情况下，``FRONT_ENVOY_YAML`` 指向 ``config/grpc-service/v3.yaml`` 文件，该文件使用 ext_authz HTTP 过滤器引导 front-envoy，此 ext_authz HTTP 过滤器使用 gRPC v3 服务（这是在 :ref:`transport_api_version 字段 <envoy_v3_api_field_extensions.filters.http.ext_authz.v3.ExtAuthz.transport_api_version>` 中指定的）。

``FRONT_ENVOY_YAML`` 的可选值可以在 ``envoy/examples/ext_authz/config`` 目录内找到。

例如，使用带有 HTTP 服务的 ext_authz HTTP 过滤器运行 Envoy 将是：

.. code-block:: console

    $ pwd
    envoy/examples/ext_authz
    $ docker-compose pull
    $ # Tearing down the currently running setup
    $ docker-compose down
    $ FRONT_ENVOY_YAML=config/http-service.yaml docker-compose up --build -d
    $ # Or you can update the .env file with the above FRONT_ENVOY_YAML value, so you don't have to specify it when running the "up" command.

步骤 4： 访问 Front Envoy 后面的上游服务
*************************************************

现在，你可以尝试通过 front-envoy 向上游服务发送请求，如下所示：

.. code-block:: console

    $ curl -v localhost:8000/service
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8000 (#0)
    > GET /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.58.0
    > Accept: */*
    >
    < HTTP/1.1 403 Forbidden
    < date: Fri, 19 Jun 2020 15:02:24 GMT
    < server: envoy
    < content-length: 0

如观察到的，该请求失败了，返回的是 ``403 Forbidden`` 状态码。这是因为 Envoy 使用的 ext_authz 过滤器拒绝了调用。为了使请求到达上游服务，你需要在 ``Authorization`` 头部添加一个 ``Bearer`` 令牌。

.. note::

    ``envoy/examples/ext_authz/auth/users.json`` 文件中定义了完整的用户列表。例如，以下示例中使用的 ``token1`` 与 ``user1`` 相对应。

下面是一个成功请求的示例：

.. code-block:: console

    $ curl -v -H "Authorization: Bearer token1" localhost:8000/service
    *   Trying 127.0.0.1...
    * TCP_NODELAY set
    * Connected to localhost (127.0.0.1) port 8000 (#0)
    > GET /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.58.0
    > Accept: */*
    > Authorization: Bearer token1
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 24
    < server: envoy
    < date: Fri, 19 Jun 2020 15:04:29 GMT
    < x-envoy-upstream-service-time: 2
    <
    * Connection #0 to host localhost left intact
    Hello user1 from behind Envoy!

我们还可以使用 `Open Policy Agent <https://www.openpolicyagent.org/>`_ 服务器（已启用 `envoy_ext_authz_grpc <https://github.com/open-policy-agent/opa-istio-plugin>`_ 插件）作为授权服务器。要运行此示例：

.. code-block:: console

    $ pwd
    envoy/examples/ext_authz
    $ docker-compose pull
    $ # Tearing down the currently running setup
    $ docker-compose down
    $ FRONT_ENVOY_YAML=config/opa-service/v2.yaml docker-compose up --build -d

并向上游服务（通过 Front Envoy）发送请求将得到：

.. code-block:: console

    $ curl localhost:8000/service --verbose
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > GET /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 28
    < server: envoy
    < date: Thu, 02 Jul 2020 06:29:58 GMT
    < x-envoy-upstream-service-time: 2
    <
    * Connection #0 to host localhost left intact
    Hello OPA from behind Envoy!

从日志中，我们可以观察到来自 Open Policy Agent 服务器的策略决策消息（和上述请求对应的策略在 ``config/opa-service/policy.rego`` 中定义）：

.. code-block:: console

    $ docker-compose logs ext_authz-opa-service | grep decision_id -A 30
    ext_authz-opa-service_1   |   "decision_id": "8143ca68-42d8-43e6-ade6-d1169bf69110",
    ext_authz-opa-service_1   |   "input": {
    ext_authz-opa-service_1   |     "attributes": {
    ext_authz-opa-service_1   |       "destination": {
    ext_authz-opa-service_1   |         "address": {
    ext_authz-opa-service_1   |           "Address": {
    ext_authz-opa-service_1   |             "SocketAddress": {
    ext_authz-opa-service_1   |               "PortSpecifier": {
    ext_authz-opa-service_1   |                 "PortValue": 8000
    ext_authz-opa-service_1   |               },
    ext_authz-opa-service_1   |               "address": "172.28.0.6"
    ext_authz-opa-service_1   |             }
    ext_authz-opa-service_1   |           }
    ext_authz-opa-service_1   |         }
    ext_authz-opa-service_1   |       },
    ext_authz-opa-service_1   |       "metadata_context": {},
    ext_authz-opa-service_1   |       "request": {
    ext_authz-opa-service_1   |         "http": {
    ext_authz-opa-service_1   |           "headers": {
    ext_authz-opa-service_1   |             ":authority": "localhost:8000",
    ext_authz-opa-service_1   |             ":method": "GET",
    ext_authz-opa-service_1   |             ":path": "/service",
    ext_authz-opa-service_1   |             "accept": "*/*",
    ext_authz-opa-service_1   |             "user-agent": "curl/7.64.1",
    ext_authz-opa-service_1   |             "x-forwarded-proto": "http",
    ext_authz-opa-service_1   |             "x-request-id": "b77919c0-f1d4-4b06-b444-5a8b32d5daf4"
    ext_authz-opa-service_1   |           },
    ext_authz-opa-service_1   |           "host": "localhost:8000",
    ext_authz-opa-service_1   |           "id": "16617514055874272263",
    ext_authz-opa-service_1   |           "method": "GET",
    ext_authz-opa-service_1   |           "path": "/service",

尝试使用除 ``GET`` 之外的方法发送请求，会被拒绝：

.. code-block:: console

    $ curl -X POST localhost:8000/service --verbose
    *   Trying ::1...
    * TCP_NODELAY set
    * Connected to localhost (::1) port 8000 (#0)
    > PUT /service HTTP/1.1
    > Host: localhost:8000
    > User-Agent: curl/7.64.1
    > Accept: */*
    >
    < HTTP/1.1 403 Forbidden
    < date: Thu, 02 Jul 2020 06:46:13 GMT
    < server: envoy
    < content-length: 0
