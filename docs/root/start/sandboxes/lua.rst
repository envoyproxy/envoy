.. _install_sandboxes_lua:

Lua filter
==========

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how `Lua <https://www.lua.org/>`_ filter can be used with the Envoy
proxy.

The Envoy proxy configuration includes three Lua filters that contain two different functions:

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

The output from the ``curl`` command below should include the headers that added by three Lua filters.

Terminal 1

.. code-block:: console

  $ curl -v localhost:8000
  *   Trying ::1...
  * TCP_NODELAY set
  * Connected to localhost (::1) port 8000 (#0)
  > GET / HTTP/1.1
  > Host: localhost:8000
  > User-Agent: curl/7.68.0
  > Accept: */*
  >
  * Mark bundle as not supporting multiuse
  < HTTP/1.1 200 OK
  < content-type: text/plain
  < date: Wed, 20 Jul 2022 16:14:13 GMT
  < content-length: 228
  < x-envoy-upstream-service-time: 27
  < header_key_2: header_value_2                   <-- This is added to the response headers by the third Lua filter. --<
  < header_key_1: header_value_1                   <-- This is added to the response headers by the second Lua filter. --<
  < response-body-size: 228                        <-- This is added to the response headers by the first Lua filter. --<
  < server: envoy
  <
  Request served by 2a3b9492f3bd

  HTTP/1.1 GET /

  Host: localhost:8000
  Accept: */*
  Foo: bar                                         <-- This is added to the request headers by the first Lua filter. --<
  User-Agent: curl/7.68.0
  X-Envoy-Expected-Rq-Timeout-Ms: 15000
  X-Forwarded-Proto: http
  X-Request-Id: 5d5ec816-9e9f-4968-bb29-72896966a219
  * Connection #0 to host localhost left intact


.. seealso::

   :ref:`Envoy Lua filter <config_http_filters_lua>`
      Learn  more about the Envoy Lua filter.

   `Lua <https://www.lua.org/>`_
      The Lua programming language.
