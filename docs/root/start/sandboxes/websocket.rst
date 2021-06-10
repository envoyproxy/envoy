.. _install_sandboxes_websocket:

WebSockets
==========

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`openssl <start_sandboxes_setup_openssl>`
        Generate ``SSL`` keys and certificates.

This example walks through some of the ways that Envoy can be configured to proxy WebSockets.

It demonstrates terminating a WebSocket connection with and without ``TLS``, and provides some basic examples
of proxying to encrypted and non-encrypted upstream sockets.

.. warning::

   For the sake of simplicity, the examples provided here do not authenticate any client certificates,
   or validate any of the provided certificates.

   When using ``TLS``, you are strongly encouraged to :ref:`validate <start_quick_start_securing_validation>`
   all certificates wherever possible.

   You should also :ref:`authenticate clients <start_quick_start_securing_mtls>`
   where you control both sides of the connection, or relevant protocols are available.

Step 1: Create a certificate file for wss
*****************************************

Change directory to ``examples/websocket`` in the Envoy repository.

.. code-block:: console

   $ pwd
   envoy/examples/websocket
   $ mkdir -p certs
   $ openssl req -batch -new -x509 -nodes -keyout certs/key.pem -out certs/cert.pem
   Generating a RSA private key
   ..................................................................................................................+++++
   ......+++++
   writing new private key to 'certs/key.pem'
   -----
   $ openssl pkcs12 -export -passout pass: -out certs/output.pkcs12 -inkey certs/key.pem -in certs/cert.pem

Step 2: Build and start the sandbox
***********************************

This starts three proxies listening on ``localhost`` ports ``10000-30000``.

It also starts two upstream services, one ``ws`` and one ``wss``.

The upstream services listen on the internal Docker network on ports ``80`` and ``443`` respectively.

The socket servers are very trivial implementations, that simply output ``[ws] HELO`` and
``[wss] HELO`` in response to any input.

.. code-block:: console

  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

              Name                             Command               State            Ports
  ---------------------------------------------------------------------------------------------------
  websocket_proxy-ws_1                /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
  websocket_proxy-wss_1               /docker-entrypoint.sh /usr ... Up      0.0.0.0:20000->10000/tcp
  websocket_proxy-wss-passthrough_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:30000->10000/tcp
  websocket_service-ws_1              websocat -E ws-listen:0.0. ... Up
  websocket_service-wss_1             websocat wss-listen:0.0.0. ... Up

Step 3: Test proxying ``ws`` -> ``ws``
**************************************

The proxy listening on port ``10000`` terminates the WebSocket connection without ``TLS`` and then proxies
to an upstream socket, also without ``TLS``.

In order for Envoy to terminate the WebSocket connection, the
:ref:`upgrade_configs <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig>`
in :ref:`HttpConnectionManager <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>`
must be set, as can be seen in the provided :download:`ws -> ws configuration <_include/websocket/envoy-ws.yaml>`:

.. literalinclude:: _include/websocket/envoy-ws.yaml
   :language: yaml
   :lines: 1-29
   :linenos:
   :emphasize-lines: 13-14

You can start an interactive session with the socket as follows:

.. code-block:: console

   $ docker run -ti --network=host solsson/websocat ws://localhost:10000
   HELO
   [ws] HELO
   GOODBYE
   [ws] HELO

Type ``Ctrl-c`` to exit the socket session.

Step 4: Test proxying ``wss`` -> ``wss``
****************************************

The proxy listening on port ``20000`` terminates the WebSocket connection with ``TLS`` and then proxies
to an upstream ``TLS`` WebSocket.

In addition to the
:ref:`upgrade_configs <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.UpgradeConfig>`
in :ref:`HttpConnectionManager <envoy_v3_api_msg_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager>`,
the :download:`wss -> wss configuration <_include/websocket/envoy-wss.yaml>` adds a ``TLS``
:ref:`transport_socket <extension_envoy.transport_sockets.tls>` to both the
:ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>` and the
:ref:`cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>`.

You can start an interactive session with the socket as follows:

.. code-block:: console

   $ docker run -ti --network=host solsson/websocat --insecure wss://localhost:20000
   HELO
   [wss] HELO
   GOODBYE
   [wss] HELO

Type ``Ctrl-c`` to exit the socket session.

Step 5: Test proxying ``wss`` passthrough
*****************************************

The proxy listening on port ``30000`` passes through all ``TCP`` traffic to an upstream ``TLS`` WebSocket.

The :download:`wss passthrough configuration <_include/websocket/envoy-wss-passthrough.yaml>` requires no ``TLS``
or ``HTTP`` setup, and instead uses a simple
:ref:`tcp_proxy  <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`.

You can start an interactive session with the socket as follows:

.. code-block:: console

   $ docker run -ti --network=host solsson/websocat --insecure wss://localhost:30000
   HELO
   [wss] HELO
   GOODBYE
   [wss] HELO

Type ``Ctrl-c`` to exit the socket session.

.. seealso::

   :ref:`Securing Envoy quick start guide <start_quick_start_securing>`
      Outline of key concepts for securing Envoy.

   :ref:`Double proxy sandbox <install_sandboxes_double_proxy>`
      An example of securing traffic between proxies with validation and
      mutual authentication using ``mTLS`` with non-``HTTP`` traffic.

   :ref:`TLS sandbox <install_sandboxes_tls>`
      Examples of various ``TLS`` termination patterns with Envoy.
