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

.. code-block::

   $ pwd
   envoy/examples/websocket
   $ mkdir -p certs
   $ openssl req -batch -new -x509 -nodes -keyout certs/key.pem -out certs/cert.pem
   $ openssl pkcs12 -export -passout pass: -out certs/output.pkcs12 -inkey certs/key.pem -in certs/cert.pem


Step 4: Build the sandbox
*************************

.. code-block:: console

  $ pwd
  envoy/examples/tls
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

         Name                            Command                 State          Ports
  -----------------------------------------------------------------------------------------------


Step 5: Test proxying ``ws`` -> ``ws``
**************************************

Step 6: Test proxying ``wss`` -> ``ws``
***************************************

Step 7: Test proxying ``wss`` -> ``wss``
****************************************

Step 8: Test proxying ``wss`` passthrough
*****************************************
