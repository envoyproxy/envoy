.. _install_sandboxes_lua_cluster_specifier:

Lua Cluster Specifier
=====================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

In this example, we show how the `Lua <https://www.lua.org/>`_ cluster specifier can be used with the
Envoy proxy.

The example Envoy proxy configuration includes a Lua cluster specifier plugin that contains a function:

- ``envoy_on_route(route_handle)``

:ref:`See here <config_http_cluster_specifier_lua>` for an overview of Envoy's Lua cluster specifier
and documentation regarding the function.

Step 1: Build the sandbox
*************************

Change to the ``examples/lua-cluster-specifier`` directory.

.. code-block:: console

  $ pwd
  envoy/examples/lua-cluster-specifier
  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

  Name                                  Command                        State   Ports
  --------------------------------------------------------------------------------------------
  lua-cluster-specifier-proxy-1         /docker-entrypoint.sh /usr ... Up      10000/tcp, 0.0.0.0:8000->8000/tcp
  lua-cluster-specifier-web_service-1   /bin/echo-server               Up      0.0.0.0:8080->8080/tcp

Step 2: Send a request to the normal service
********************************************

The output from the ``curl`` command below should return 200, since the lua code select the normal service.

Terminal 1

.. code-block:: console

   $ curl -i localhost:8000/anything 2>&1 |grep 200
   HTTP/1.1 200 OK

Step 3: Send a request to the fake service
******************************************

The output from the ``curl`` command below should return 503, since the lua code select the fake service.

Terminal 1

.. code-block:: console

   $ curl -i localhost:8000/anything -H "header_key:fake" 2>&1 |grep 503
   HTTP/1.1 503 Service Unavailable

.. seealso::

   :ref:`Envoy Lua cluster specifier <config_http_cluster_specifier_lua>`
      Learn  more about the Envoy Lua cluster specifier.

   `Lua <https://www.lua.org/>`_
      The Lua programming language.
