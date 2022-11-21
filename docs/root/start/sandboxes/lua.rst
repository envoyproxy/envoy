.. _install_sandboxes_lua:

Lua filter
==========

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how the `Lua <https://www.lua.org/>`_ filter can be used with the Envoy
proxy.

The example Envoy proxy configuration includes two Lua filters that contain two different functions:

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

The output from the ``curl`` command below should include the header added by the Lua filter.

Terminal 1

.. code-block:: console

   $ curl -v localhost:8000 2>&1 | grep Foo
   Foo: bar                              <-- This is added by the common Lua filter. --<

Step 3: Using multiple Lua filters at the same time
*********************************************************

Two Lua filters are configured in the example Envoy proxy configuration. But the second one can only work at a
specfic route.

The output from the ``curl`` command below should include the headers that added by multiple Lua filters.

Terminal 1

.. code-block:: console

   curl -v localhost:8000/multiple/lua/scripts 2>&1 | grep header_key_1
   < header_key_1: header_value_1        <-- This is added by the second route-specific Lua filter. --<

.. seealso::

   :ref:`Envoy Lua filter <config_http_filters_lua>`
      Learn  more about the Envoy Lua filter.

   `Lua <https://www.lua.org/>`_
      The Lua programming language.
