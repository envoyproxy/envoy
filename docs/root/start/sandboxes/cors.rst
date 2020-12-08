.. _install_sandboxes_cors:

CORS 过滤器
===========

跨域资源共享（CORS）是一种对资源实施客户端访问控制的方法，该方法通过指定外部域名来访问你域名下的某些或者全部路由。浏览器使用响应的 HTTP headers 来确定是否接受跨域的响应报文。

为了演示 front-envoy 如何执行 CORS 策略，我们发布了一组 `docker compose <https://docs.docker.com/compose/>` 沙盒，这组沙盒是在不同的 Origin 上部署前端和后端服务，这些服务都在 front-envoy 后面。

前端服务具有一个用于输入后端的远程域名字段的服务以及单选按钮以选择远程域名的 CORS 执行策略。CORS 执行策略选项：

  * Disabled: 在请求的路由上禁用了 CORS。这将导致客户端 CORS 错误，因为有效的 CORS 请求必需携带的 http headers 没有被提供。
  * Open: 在请求的路由上启用了 CORS，但设置了允许的 Origin 为 ``*``。这是一项非常宽松的策略，意味着任何 Origin 都可以请求这个端点上的数据。
  * Restricted: 在请求的路由上启用了 CORS ，并且仅允许 Origin 为 ``envoyproxy.io``。这将导致客户端 CORS 错误。

运行沙盒
~~~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

第 3 步：启动我们所有的容器
*****************************

切换到 ``cors`` 示例中的 ``frontend`` 目录，然后启动容器：

.. code-block:: console

  $ pwd
  envoy/examples/cors/frontend
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

            Name                          Command              State                            Ports
  ------------------------------------------------------------------------------------------------------------------------------
  frontend_front-envoy_1        /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
  frontend_frontend-service_1   /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp

现在，在 ``cors`` 示例中切换到 ``backend`` 目录，并启动容器：

.. code-block:: console

  $ pwd
  envoy/examples/cors/backend
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

            Name                         Command             State                            Ports
  ----------------------------------------------------------------------------------------------------------------------------
  backend_backend-service_1   /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
  backend_front-envoy_1       /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8002->8000/tcp, 0.0.0.0:8003->8001/tcp

第 4 步：测试 Envoy 的 CORS 功能
***********************************

现在，你可以在浏览器上打开 http://localhost:8000 来查看前端服务。

跨域请求的结果将显示在 *Request Results* 下的页面上。

你可以在浏览器控制台中找到浏览器的 ``CORS`` 执行日志。

例如：

.. code-block:: console

  Access to XMLHttpRequest at 'http://192.168.99.100:8002/cors/disabled' from origin 'http://192.168.99.101:8000'
  has been blocked by CORS policy: No 'Access-Control-Allow-Origin' header is present on the requested resource.

第 5 步：通过 admin 检查后端的统计信息
***************************************

当 Envoy 运行时，如果配置了端口，它可以监听 ``admin`` 请求。

在示例配置中，后端 admin 绑定到端口 ``8003``。

如果你用浏览器打开 http://localhost:8003/stats，则可以查看所有 Envoy 关于后端统计信息。如果你从前端群集发出请求，你应该能看到的 ``CORS`` 统计信息中无效或者有效的请求数会增加。

.. code-block:: none

  http.ingress_http.cors.origin_invalid: 2
  http.ingress_http.cors.origin_valid: 7
