.. _install_sandboxes_zipkin_tracing:

Zipkin 追踪（trace）
=====================

Zipkin 追踪沙盒使用 Zipkin <https://zipkin.io/> 作为追踪提供程序来实现 Envoy 的 :ref:请求追踪功能。
这个沙盒与前面所讲的前端代理架构非常的相似，但有一点不同的是：在返回响应之前，service1 会对 service2 进行 API 调用。 
这三个容器将被部署在名为 ``envoymesh`` 的虚拟网络中。

所有传入的请求都通过前端 envoy 进行路由，envoy 充当位于 ``envoymesh`` 网络边缘的反向代理。
端口 ``8000`` 由 docker compose 暴露（参见 :repo:`/examples/zipkin-tracing/docker-compose.yaml`）。
请注意，所有 envoy 都配置为收集请求跟踪 (例如 :repo:`/examples/zipkin-tracing/front-envoy-zipkin.yaml`中的 http_connection_manager/config/tracing 配置) 并设置为传递 Zipkin 追踪器生成的 span 到 Zipkin 集群中 (追踪驱动设置在 :repo:`/examples/zipkin-tracing/front-envoy-zipkin.yaml`)。

在将请求路由到合适的 envoy 或应用程序之前，Envoy 会生成合适的用于追踪的 span（父子共享的上下文 span）。
在高层次上，每个 span 记录上游API调用的延迟以及将 span 与其他相关 span（例如跟踪ID）关联所需的信息。 

从 Envoy 进行跟踪的最重要的好处之一是，它会保证将跟踪信息传播到 Zipkin 服务群集。
但是，为了充分利用跟踪，应用端必须在调用其他服务的时候传递 Envoy 生成的追踪的请求头信息。
在我们提供的沙箱例子中，作为 service1 的简单 flask 应用（参见 :repo:`/examples/front-proxy/service.py` 中的跟踪函数）在访问 service2 的时候传递了追踪的请求头信息。


运行沙盒
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3: 构建沙盒
*************************

要构建这个沙盒示例，并启动示例应用程序，请运行以下命令：

.. code-block:: console

    $ pwd
    envoy/examples/zipkin-tracing
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

                Name                          Command             State                            Ports
    -----------------------------------------------------------------------------------------------------------------------------
    zipkin-tracing_front-envoy_1   /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
    zipkin-tracing_service1_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
    zipkin-tracing_service2_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
    zipkin-tracing_zipkin_1        /busybox/sh run.sh             Up      9410/tcp, 0.0.0.0:9411->9411/tcp

步骤 4: 发送请求
**************************

你现在可以通过 envoy 向 service1 发送请求，如下所示：

.. code-block:: console

    $ curl -v localhost:8000/trace/1
    *   Trying 192.168.99.100...
    * Connected to 192.168.99.100 (192.168.99.100) port 8000 (#0)
    > GET /trace/1 HTTP/1.1
    > Host: 192.168.99.100:8000
    > User-Agent: curl/7.43.0
    > Accept: */*
    >
    < HTTP/1.1 200 OK
    < content-type: text/html; charset=utf-8
    < content-length: 89
    < x-envoy-upstream-service-time: 1
    < server: envoy
    < date: Fri, 26 Aug 2018 19:39:19 GMT
    <
    Hello from behind Envoy (service 1)! hostname: f26027f1ce28 resolvedhostname: 172.19.0.6
    * Connection #0 to host 192.168.99.100 left intact

步骤 5: 在 Zipkin UI 中查看追踪
*******************************************

使用你的浏览器访问 http://localhost:9411。
你应该看到 Zipkin 仪表板。
将服务设置为 “front-proxy”，并将开始时间设置为在测试（步骤 2）开始前几分钟并按回车。
你应该能看到来自 front-proxy 的跟踪信息。
单击一个追踪来显示从 front-proxy 到 service1 再到 service2 的路径信息，以及每个跳跃点所产生的延迟。
