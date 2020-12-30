.. _install_sandboxes_csrf:

CSRF 过滤器
=============

跨站请求伪造（CSRF）是一种攻击，当恶意的第三方站点利用一个可以让他们以用户的行为方式来提交非期望的请求时，这种攻击就发生了。为了减轻这种攻击，此过滤器会检查请求的来源，以确定请求的来源和目的地是否相同。

为了演示 front-envoy 是如何强制执行 CSRF 策略的，我们发布了一个 `docker compose <https://docs.docker.com/compose/>`_ 沙盒环境，来同时部署一个前端和后端服务。此服务将在两个具有不同来源的不同虚拟机上启动。

前端有一个字段用来输入你希望发送 POST 请求的远程域，以及一个单选按钮，按钮用于选择远程域 CSRF 强制项。CSRF 强制选项有： 
  * 禁止：CSRF 在请求路由中被禁止。因为没有 CSRF 强制执行功能，这将使得请求能够成功。
  * 影子模式：CSRF 在请求路由中没有被强制执行，但是将会记录请求是否包含有效的来源信息。
  * 开启：CSRF 处于开启状态，当请求来自于一个不同的来源时，会返回一个 403（禁止）状态码。
  * 忽略：CSRF 处于开启状态，但是请求的类型是 GET。这将绕过 CSRF 过滤器且成功返回。

运行沙盒
~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3：启动我们的所有容器
*****************************

在 ``csrf`` 示例中，切换至 ``samesite`` 目录，然后启动容器：

.. code-block:: console

  $ pwd
  envoy/examples/csrf/samesite
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

            名称                        命令                状态                            端口
  ----------------------------------------------------------------------------------------------------------------------
  samesite_front-envoy_1      /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
  samesite_service_1          /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp

现在，在 ``csrf`` 示例中，切换至 ``crosssite`` 目录，然后启动容器：

.. code-block:: console

  $ pwd
  envoy/examples/csrf/crosssite
  $ docker-compose up --build -d
  $ docker-compose ps

            名称                       命令                  状态                            端口
  ----------------------------------------------------------------------------------------------------------------------
  crosssite_front-envoy_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 0.0.0.0:8002->8000/tcp, 0.0.0.0:8003->8001/tcp
  crosssite_service_1          /docker-entrypoint.sh /bin ... Up      10000/tcp, 8000/tcp

步骤 4：测试 Envoy 的 CSRF 能力
*********************************

现在，你可以打开一个浏览器，输入 http://localhost:8002 来查看 ``crosssite`` 前端服务。

输入 ``samesite`` 机器的 IP 来演示跨站点请求。开启了强制执行功能的请求将会失败。默认情况下，此字段将填充为 ``localhost``。

为了演示同站点请求，在 http://localhost:8000 中打开 ``samesite`` 前端服务，将 ``samesite`` 机器的 IP 地址当作目的地来输入。

跨站点请求的结果将展示在页面的 *Request Results* 下。你会在浏览器控制台和网络选项卡中找到浏览器的 ``CSRF`` 强制执行日志。

例如：

.. code-block:: console

  加载资源失败：服务器返回了 403（禁止） 状态码

如果你将目的地址改为和展示网站一样，且开启了 ``CSRF`` 强制执行，则请求能顺利通过。

步骤 5：通过 admin 来查看后端统计
************************************

当 Envoy 运行时，如果配置了监听端口，它能够监听 ``admin`` 请求。在示例配置中，后端 admin 被绑定到了 ``8001`` 端口。

如果你浏览 http://localhost:8001/stats，你将能够看到后端的所有 Envoy 统计。当你从前置集群发出请求时，你应该能够看到无效和有效来源的 CORS 统计信息在增加。

.. code-block:: none

  http.ingress_http.csrf.missing_source_origin: 0
  http.ingress_http.csrf.request_invalid: 1
  http.ingress_http.csrf.request_valid: 0
