.. _install_sandboxes_tls:

TLS
===

This example walks through some of the ways that Envoy can be configured to make
use of encrypted connections using ``TLS`` over ``HTTP``.

It demonstrates a number of commonly used proxying and termination patterns:

- ``https`` -> ``http``
- ``https`` -> ``https``
- ``http`` -> ``https``

.. include:: _include/docker-env-setup.rst

Change directory to ``examples/tls`` in the Envoy repository.

Step 3: Build the sandbox
*************************

.. code-block:: console

  $ pwd
  envoy/examples/tls
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

         Name                         Command                 State          Ports
  --------------------------------------------------------------------------------------------
  tls_proxy-https-to-http_1    /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
  tls_proxy-https-to-https_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:10001->10000/tcp
  tls_proxy-http-to-https_1    /docker-entrypoint.sh /usr ... Up      0.0.0.0:10002->10000/tcp
  tls_service-http_1           node ./index.js                Up
  tls_service-https_1          node ./index.js                Up

Step 4: Test proxying ``https`` -> ``http``
********************************************

The Envoy proxy listening on https://localhost:10000 terminates ``HTTPS`` and proxies to the interal ``HTTP`` server

.. code-block:: console

   $ curl -sk https://localhost:10000  | jq  '.headers["x-forwarded-proto"]'
   "https"

   $ curl -sk https://localhost:10000  | jq  '.os.hostname'
   "service-http"

Step 5: Test proxying ``https`` -> ``https``
********************************************

The Envoy proxy listening on https://localhost:10001 terminates ``HTTPS`` and proxies to the interal ``HTTPS`` server

.. code-block:: console

   $ curl -sk https://localhost:10001  | jq  '.headers["x-forwarded-proto"]'
   "https"

   $ curl -sk https://localhost:10001  | jq  '.os.hostname'
   "service-https"

Step 6: Test proxying ``http`` -> ``https``
*******************************************

The Envoy proxy listening on https://localhost:10002 terminates ``HTTP`` and proxies to the interal ``HTTPS`` server

.. code-block:: console

   $ curl -s http://localhost:10002  | jq  '.headers["x-forwarded-proto"]'
   "http"

   $ curl -s http://localhost:10002  | jq  '.os.hostname'
   "service-https"
