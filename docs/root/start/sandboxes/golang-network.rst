.. _install_sandboxes_golang_network:

Golang network filter
=====================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`netcat <start_sandboxes_setup_netcat>`
        Used to send TCP data.

In this example, we show how the `Golang <https://go.dev/>`_ network filter can be used with the Envoy proxy. We also show how the Go plugins can be independently configured at runtime.

The example Go plugin adds a :literal:`hello, \ ` prefix to the requests received from its TCP connections. These modified requests are then proxied to an echo service that is retrieved from the configuration file.

.. code-block:: yaml
   :emphasize-lines: 4

   plugin_config:
      "@type": type.googleapis.com/xds.type.v3.TypedStruct
      value:
         echo_server_addr: echo_service


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

   NAME                            COMMAND                  SERVICE             STATUS              PORTS
   golang-network-echo_service-1   "/tcp-echo"              echo_service        running
   golang-network-proxy-1          "/docker-entrypoint.…"   proxy               running             0.0.0.0:10000->10000/tcp

In this example, we start up two containers - an echo service which simply responds to what it has received from its TCP connections, and a proxy service that utilizes a Golang plugin to process and proxy data to the echo service.

Step 3: Send some data to be handled by the Go plugin
*****************************************************

The response from the ``nc`` command below should include the :literal:`hello, \ ` prefix which will be added by the example Go plugin.

.. code-block:: console

   $ echo "world" | nc localhost 10000 2>&1
   < hello, world

.. seealso::

   :ref:`Envoy Go network filter <config_network_filters_golang>`
      Further information about the Envoy Go network filter.
   :ref:`Envoy Go HTTP filter <config_http_filters_golang>`
      Further information about the Envoy Go HTTP filter.
   :repo:`Go plugin API <contrib/golang/common/go/api/filter.go>`
      Overview of Envoy's Go plugin APIs.
