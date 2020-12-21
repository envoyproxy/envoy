.. _install_sandboxes_cache_filter:

缓存过滤器
============
.. TODO(yosrym93): When a documentation is written for a production-ready Cache Filter, link to it through this doc.

本示例将展示如何使用 Envoy 的缓存过滤器处理 HTTP 缓存。
沙盒环境的安装需要基于 :ref: `前端代理沙盒 <install_sandboxes_front_proxy>` 的设置。

所有传入请求都通过前端 Envoy 进行路由，该前端 Envoy 充当位于 ``envoymesh`` 网络边缘的反向代理。
docker compose 暴露两个端口 ``8000`` 和 ``8001`` 来分别处理服务的 ``HTTP`` 调用和对 ``/admin`` 的请求。（请参阅 :repo: `/examples/cache/docker-compose.yaml`）。
前端 Envoy 的后面部署了两个后端服务，每个后端服务都有一个 sidecar Envoy。

前端 Envoy 配置为运行缓存过滤器，该过滤器将可缓存的响应存储在内存缓存中，并将其提供给后续请求。
示例中，由部署的服务提供的响应信息已配置在 :repo: `/examples/cache/responses.yaml` 文件中。
该文件已安装到两个服务所在的容器中，因此在服务运行时对存储的响应信息所做的任何更改都将立即生效（无需重新构建项目或重启）。

为了演示的目的，响应的创建时间已经提前配置在它的响应体中。为了验证的目的，将为每个响应计算一个 Etag，该 Etag 仅取决于 yaml 文件中的响应体（即，不考虑附加时间）。
通过带有 ``age`` 的头信息来识别缓存的响应。比较生成时间是否早于响应头中包含的 ``date``，可验证响应信息的有效性；一个通过验证的响应信息，它头信息中的 ``date`` 会更新，而响应体不会改变。
通过验证的响应头信息中不包含 ``age`` 信息。来自后端服务的响应头中不包含 ``age`` 信息，其头信息中的 ``date`` 信息与响应生成的时间一致。

运行沙盒环境
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

第三步，启动我们的所有容器
***********************************

.. code-block:: console

    $ pwd
    envoy/examples/cache
    $ docker-compose build --pull
    $ docker-compose up -d
    $ docker-compose ps

           名称                      命令                 状态                           端口
    ------------------------------------------------------------------------------------------------------------------------
    cache_front-envoy_1   /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
    cache_service1_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp
    cache_service2_1      /bin/sh -c /usr/local/bin/ ... Up      10000/tcp, 8000/tcp

第四步，测试 Envoy 的 HTTP 缓存能力
**********************************************

现在你可以通过 ``front-envoy`` 向后端发送一个请求。需注意，两个服务是不同的请求路由，相同的请求发送到不同服务时产生的缓存实体是不同的。（即，一个请求发送到服务 2 产生的缓存响应信息与发送到服务 1 所产生的缓存响应信息不是同一个对象）。

发送一个请求：

``curl -i localhost:8000/service/<service_no>/<response>``

``service_no``：请求发送到服务 1 或 2。

``response``：请求后的响应。 响应信息可在 :repo:`/examples/cache/responses.yaml` 文件中查看。

提供的示例响应信息如下：

- 一分钟有效
    响应缓存仅保持一分钟。之后，响应将由后端服务验证后再从缓存中提供。如果缓存被更新，则返回新的响应（及缓存）。 否则，将缓存并响应缓存的响应。

- 私有
    这种响应模式为私有的；响应不能被存储在共享缓存中（例如：代理中）。响应永远都需要由后端服务返回。

- 无缓存
    这种模式的响应每次在返回前都需要被验证。

在运行沙盒测试的过程中，你可以改变响应头信息及响应体（或者新增一个响应）。

响应示例
-----------------

1. 一分钟有效
^^^^^^^^^^^^^^^^^^^

.. code-block:: console

    $ curl -i localhost:8000/service/1/valid-for-minute
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 103
    cache-control: max-age=60
    custom-header: any value
    etag: "172ae25df822c3299cf2248694b4ce23"
    date: Fri, 11 Sep 2020 03:20:40 GMT
    server: envoy
    x-envoy-upstream-service-time: 11

    该响应将在缓存中保持一分钟
    响应体生成时间为：Fri, 11 Sep 2020 03:20:40 GMT

当然，该响应头信息中的 ``date`` 信息与生成时间是一致的。
30 秒后发送相同的请求会得到与之前相同的一个响应，且该响应的生成时间也是相同的。
不同的是，这个来自缓存中的响应，它的响应头中会带一个 ``age`` 信息：

.. code-block:: console

    $ curl -i localhost:8000/service/1/valid-for-minute
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 103
    cache-control: max-age=60
    custom-header: any value
    etag: "172ae25df822c3299cf2248694b4ce23"
    date: Fri, 11 Sep 2020 03:20:40 GMT
    server: envoy
    x-envoy-upstream-service-time: 11
    age: 30

    该响应将在缓存中保持一分钟
    响应体生成时间为：Fri, 11 Sep 2020 03:20:40 GMT

一分零一秒后：

.. code-block:: console

    $ curl -i localhost:8000/service/1/valid-for-minute
    HTTP/1.1 200 OK
    cache-control: max-age=60
    custom-header: any value
    etag: "172ae25df822c3299cf2248694b4ce23"
    date: Fri, 11 Sep 2020 03:21:41 GMT
    server: envoy
    x-envoy-upstream-service-time: 8
    content-length: 103
    content-type: text/html; charset=utf-8

    该响应将在缓存中保持一分钟
    响应体生成时间为：Fri, 11 Sep 2020 03:20:40 GMT

在后端服务验证过后会返回一个相同的响应。
你可以发现这个返回的响应的生成时间与之前的是一样的，但是响应头中的 ``date`` 已被更新为验证响应的时间。
同时，在响应头中不包含 ``age`` 信息。

这种模式的响应每次被验证后，响应信息都会在缓存中保持一分钟。
如果缓存中的响应依然处于有效期，即使响应体发生了变化，Envoy 依然会返回缓存中的响应信息。缓存中的响应信息仅当缓存失效后才会被更新。

2. 私有
^^^^^^^^^^

.. code-block:: console

    $ curl -i localhost:8000/service/1/private
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 117
    cache-control: private
    etag: "6bd80b59b2722606abf2b8d83ed2126d"
    date: Fri, 11 Sep 2020 03:22:28 GMT
    server: envoy
    x-envoy-upstream-service-time: 7

    这是个私有的响应，它不会被缓存在 Envoy 中。
    响应体生成时间为：Fri, 11 Sep 2020 03:22:28 GMT

无论你请求多少次，你每次都会得到一个新的响应信息；新的生成时间、新的 ``date`` 头信息，且响应头中不存在 ``age`` 信息。

3. 无缓存
^^^^^^^^^^^

.. code-block:: console

    $ curl -i localhost:8000/service/1/no-cache
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 130
    cache-control: max-age=0, no-cache
    etag: "ce39a53bd6bb8abdb2488a5a375397e4"
    date: Fri, 11 Sep 2020 03:23:07 GMT
    server: envoy
    x-envoy-upstream-service-time: 7

    这个响应会被缓存，但是每次请求时都会被校验。
    响应体生成时间为：Fri, 11 Sep 2020 03:23:07 GMT

几秒后:

.. code-block:: console

    $ curl -i localhost:8000/service/1/no-cache
    HTTP/1.1 200 OK
    cache-control: max-age=0, no-cache
    etag: "ce39a53bd6bb8abdb2488a5a375397e4"
    date: Fri, 11 Sep 2020 03:23:12 GMT
    server: envoy
    x-envoy-upstream-service-time: 7
    content-length: 130
    content-type: text/html; charset=utf-8

    这个响应会被缓存，但是每次请求时都会被校验。
    响应体生成时间为：Fri, 11 Sep 2020 03:23:07 GMT

你会收到一个生成时间相同的缓存响应。
但是，响应头中的 ``date`` 信息永远是新的，因为这类响应在返回前始终会先被验证。
另外，响应头中不存在 ``age`` 信息。

如果你修改了 yaml 文件中的响应信息：

.. code-block:: console

    $ curl -i localhost:8000/service/1/no-cache
    HTTP/1.1 200 OK
    content-type: text/html; charset=utf-8
    content-length: 133
    cache-control: max-age=0, no-cache
    etag: "f4768af0ac9f6f54f88169a1f3ecc9f3"
    date: Fri, 11 Sep 2020 03:24:10 GMT
    server: envoy
    x-envoy-upstream-service-time: 7

    这个响应会被缓存，但是每次请求时都会被校验！！！
    响应体生成时间为：Fri, 11 Sep 2020 03:24:10 GMT

后端服务会返回一个新的响应。
新的响应会被缓存，并用于接下来的请求。

当然，你也可以在 yaml 文件中加一个 ``cache-control`` 信息不同的响应信息去测试！
欲了解更多缓存和 ``cache-control`` 相关的信息，请访问 `MDN Web Docs <https://developer.mozilla.org/en-US/docs/Web/HTTP/Caching>`_.
