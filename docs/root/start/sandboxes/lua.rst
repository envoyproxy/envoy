.. _install_sandboxes_lua:

Lua 过滤器
==========

在这个例子中，我们会展示 Lua 过滤器在 Envoy 代理中是如何使用的。 Envoy 代理配置包括一个 Lua 过滤器，此过滤器包含了在 :ref:`这个 <config_http_filters_lua>` 文档中记录的两个函数，即 ``envoy_on_request(request_handle)`` 和 ``envoy_on_response(response_handle)`` 。

运行沙盒
~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3：构建沙盒
****************

.. code-block:: console

  $ pwd
  envoy/examples/lua
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                     Command               State                            Ports
  --------------------------------------------------------------------------------------------------------------------
  lua_proxy_1         /docker-entrypoint.sh /bin       Up      10000/tcp, 0.0.0.0:8000->8000/tcp, 0.0.0.0:8001->8001/tcp
  lua_web_service_1   node ./index.js                  Up      0.0.0.0:8080->80/tcp

Step 4: 给服务发送请求
**********************


下面 ``curl`` 命令的输出结果应该包含头部 ``foo`` 。

终端 1

.. code-block:: console

  $ curl -v localhost:8000

     Trying ::1...
  * TCP_NODELAY set
  * Connected to localhost (::1) port 8000 (#0)
  > GET / HTTP/1.1
  > Host: localhost:8000
  > User-Agent: curl/7.64.1
  > Accept: */*
  >
  < HTTP/1.1 200 OK
  < x-powered-by: Express
  < content-type: application/json; charset=utf-8
  < content-length: 544
  < etag: W/"220-IhsqVTh4HjcpuJQ3C+rEL1Cw1jA"
  < date: Thu, 31 Oct 2019 03:13:24 GMT
  < x-envoy-upstream-service-time: 1
  < response-body-size: 544                      <-- This is added to the response header by our Lua script. --<
  < server: envoy
  <
  {
    "path": "/",
    "headers": {
      "host": "localhost:8000",
      "user-agent": "curl/7.64.1",
      "accept": "*/*",
      "x-forwarded-proto": "http",
      "x-request-id": "a78fcce7-2d67-4eeb-890a-73eebb942a17",
      "foo": "bar",                              <-- This is added to the request header by our Lua script. --<
      "x-envoy-expected-rq-timeout-ms": "15000",
      "content-length": "0"
    },
    "method": "GET",
    "body": "",
    "fresh": false,
    "hostname": "localhost",
    "ip": "::ffff:172.20.0.2",
    "ips": [],
    "protocol": "http",
    "query": {},
    "subdomains": [],
    "xhr": false,
    "os": {
      "hostname": "7ca39ead805a"
    }
  * Connection #0 to host localhost left intact
  }* Closing connection 0
