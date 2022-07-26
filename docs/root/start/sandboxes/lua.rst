.. _install_sandboxes_lua:

Lua filter
==========

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how `Lua <https://www.lua.org/>`_ filter can be used with the Envoy
proxy.

The Envoy proxy configuration includes two Lua filters that contain two different functions:

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

The output from the ``curl`` command below should include the header added by Lua filter.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8000
   * Rebuilt URL to: localhost:8000/
   *   Trying ::1...
   * TCP_NODELAY set
   * Connected to localhost (::1) port 8000 (#0)
   > GET / HTTP/1.1
   > Host: localhost:8000
   > User-Agent: curl/7.58.0
   > Accept: */*
   >
   < HTTP/1.1 200 OK
   < content-type: text/plain
   < date: Tue, 26 Jul 2022 03:56:24 GMT
   < content-length: 227
   < x-envoy-upstream-service-time: 36
   < server: envoy
   <
   Request served by d4f873a2ef0e

   HTTP/1.1 GET /

   Host: localhost:8000
   Accept: */*
   Foo: bar                                    <-- This is added by the common Lua filter. --<
   User-Agent: curl/7.58.0
   X-Envoy-Expected-Rq-Timeout-Ms: 15000
   X-Forwarded-Proto: http
   X-Request-Id: 9e2d0bef-af11-4751-9fef-c6a13d20859a
   * Connection #0 to host localhost left intact

Step 3: Using multiple Lua filters at the same time
*********************************************************

Two Lua filters are configured in the example Envoy proxy configuration. But the second one can only work at a
specfic route.
The output from the ``curl`` command below should include the headers that added by multiple Lua filters.

Terminal 1

.. code-block:: console

   curl -v localhost:8000/multiple/lua/scripts
   *   Trying ::1...
   * TCP_NODELAY set
   * Connected to localhost (::1) port 8000 (#0)
   > GET /multiple/lua/scripts HTTP/1.1
   > Host: localhost:8000
   > User-Agent: curl/7.58.0
   > Accept: */*
   >
   < HTTP/1.1 200 OK
   < content-type: text/plain
   < date: Tue, 26 Jul 2022 04:09:48 GMT
   < content-length: 247
   < x-envoy-upstream-service-time: 0
   < header_key_1: header_value_1              <-- This is added by the second route-specific Lua filter. --<
   < server: envoy
   <
   Request served by d4f873a2ef0e

   HTTP/1.1 GET /multiple/lua/scripts

   Host: localhost:8000
   Accept: */*
   Foo: bar                                    <-- This is added by the common Lua filter. --<
   User-Agent: curl/7.58.0
   X-Envoy-Expected-Rq-Timeout-Ms: 15000
   X-Forwarded-Proto: http
   X-Request-Id: f3213085-f9e3-40f6-af61-4d6168fb3f21
   * Connection #0 to host localhost left intact


.. seealso::

   :ref:`Envoy Lua filter <config_http_filters_lua>`
      Learn  more about the Envoy Lua filter.

   `Lua <https://www.lua.org/>`_
      The Lua programming language.
