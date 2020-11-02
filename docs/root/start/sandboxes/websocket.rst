.. _install_sandboxes_websocket:

WebSockets
==========

This example walks through some of the ways that Envoy can be configured to proxy WebSockets.

It demonstrates terminating a WebSocket connection with and without TLS, and provides some basic examples
of proxying to secure upstream sockets.

.. include:: _include/docker-env-setup.rst

Change directory to ``examples/websocket`` in the Envoy repository.

Step 3: Create a certificate file for wss
*****************************************

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


Step 4: Build the sandbox
*************************

This starts four proxies listening on ``localhost`` ports ``10000-40000``.

It also starts two upstream services, one ``ws`` and one ``wss``.

.. code-block:: console

  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps
              Name                             Command               State            Ports
  ---------------------------------------------------------------------------------------------------
  websocket_proxy-ws_1                /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
  websocket_proxy-wss-ws_1            /docker-entrypoint.sh /usr ... Up      0.0.0.0:20000->10000/tcp
  websocket_proxy-wss-wss_1           /docker-entrypoint.sh /usr ... Up      0.0.0.0:30000->10000/tcp
  websocket_proxy-wss-passthrough_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:40000->10000/tcp
  websocket_service-ws_1              websocat -E ws-listen:0.0. ... Up
  websocket_service-wss_1             websocat wss-listen:0.0.0. ... Up

Step 5: Test proxying ``ws`` -> ``ws``
**************************************

The proxy listening on port ``10000`` terminates the WebSocket connection without TLS and then proxies
to an upstream socket, also without TLS.

You can start an interactive session with the socket as follows:

.. code-block:: console

   $ docker run -ti --network=host solsson/websocat ws://localhost:10000
   HELO
   [ws] HELO
   GOODBYE
   [ws] HELO

The socket server is a very trivial implementation, that simply outputs ``[ws] HELO`` in response to
any input.

Type ``Ctrl-c`` to exit the socket session.


Step 6: Test proxying ``wss`` -> ``ws``
***************************************

Step 7: Test proxying ``wss`` -> ``wss``
****************************************

Step 8: Test proxying ``wss`` passthrough
*****************************************
