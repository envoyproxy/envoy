.. _install_sandboxes_lua:

Lua filter
==========

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how a `Lua <https://www.lua.org/>`_ filter can be used with the Envoy
proxy.

The Envoy proxy configuration includes a Lua filter that contains two functions:

- ``envoy_on_request(request_handle)``
- ``envoy_on_response(response_handle)``

:ref:`See here <config_http_filters_lua>` for an overview of Envoy's Lua filter and documentation
regarding these functions.

Step 1: Build the sandbox
*************************

Change to the ``examples/lua`` directory.

.. code-block:: console

  $ pwd
  envoy/examples/lua
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

      Name                     Command               State             Ports
  --------------------------------------------------------------------------------------------
  lua_proxy_1         /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp
  lua_web_service_1   node ./index.js                Up      0.0.0.0:8080->80/tcp

Step 2: Send a request to the service
*************************************

The output from the ``curl`` command below should include the headers ``foo``.

Terminal 1

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

.. seealso::

   :ref:`Envoy Lua filter <config_http_filters_lua>`
      Learn  more about the Envoy Lua filter.

   `Lua <https://www.lua.org/>`_
      The Lua programming language.
