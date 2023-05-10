.. _install_sandboxes_golang_network:

Golang Network Filter
=====================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`netcat <start_sandboxes_setup_netcat>`
        Used to send ``TCP`` data.

In this example, we show how the `Golang <https://go.dev/>`_ network filter can be used with the Envoy
proxy.

The example demonstrates a Go plugin that can process tcp data stream directly.

It also shows how Go plugins can be independently configured at runtime.

Step 1: Compile the go plugin library
*************************************

Change to the ``examples/golang-network`` directory and build the go plugin library.

.. code-block:: console

   $ pwd
   envoy/examples/golang-network
   $ docker compose -f docker-compose-go.yaml run --rm go_plugin_compile

The compiled library should now be in the ``lib`` folder.

.. code-block:: console

   $ ls lib
   simple.so

Step 2: Start all of our containers
***********************************

Start all the containers.

.. code-block:: console

  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

  NAME                            COMMAND                  SERVICE        STATUS   PORTS
  golang-network-echo_service-1   "/tcp-echo"              echo_service   Up       0.0.0.0:1025->1025/tcp
  golang-network-proxy-1          "/docker-entrypoint.…"   proxy          Up       0.0.0.0:10000->10000/tcp

Step 3: Send some data to be handled by the Go plugin
*****************************************************

The output from the ``nc`` command below should include the "hello, " prefix added by the simple Go plugin.

.. code-block:: console

   $ echo "world" | nc localhost 10000 2>&1
   < hello, world

.. seealso::

   :ref:`Envoy Go Network Filter <config_network_filters_golang>`
      Further information about the Envoy Go Network filter.
   :ref:`Envoy Go HTTP Filter <config_http_filters_golang>`
      Further information about the Envoy Go HTTP filter.
   :ref:`Go extension API <envoy_v3_api_file_contrib/envoy/extensions/filters/http/golang/v3alpha/golang.proto>`
      The Go extension filter API.
   :repo:`Go plugin API <contrib/golang/common/go/pkg/api/filter.go>`
      Overview of Envoy's Go plugin APIs.
