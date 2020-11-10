.. _install_sandboxes_tls:

TLS
===

This example walks through some of the ways that Envoy can be configured to make
use of encrypted connections using ``HTTP`` over ``TLS`` .

It demonstrates a number of commonly used proxying and ``TLS`` termination patterns:

- ``https`` -> ``http``
- ``https`` -> ``https``
- ``http`` -> ``https``
- ``https`` passthrough

.. note::

   Envoy can also proxy non-``HTTP`` traffic over ``TLS``.
   Please the double proxy sandbox <INSERT LINK HERE> for an example of this.

.. warning::

   For the sake of simplicity, the examples provided here do not authenticate any client certificates,
   or validate any of the provided certificates.

   Please see :ref:`securing envoy <start_quick_start_securing>` for more information about using ``TLS`` to secure your network.

   The :ref:`double proxy sandbox <>` also provides an example of validation and authentication
   with client certificates using ``mTLS``.

.. include:: _include/docker-env-setup.rst

Change directory to ``examples/tls`` in the Envoy repository.

Step 3: Build the sandbox
*************************

This starts four proxies listening on ``localhost`` ports ``10000-10003``.

It also starts two upstream services, one ``HTTP`` and one ``HTTPS``.

.. code-block:: console

  $ pwd
  envoy/examples/tls
  $ docker-compose pull
  $ docker-compose up --build -d
  $ docker-compose ps

         Name                            Command                 State          Ports
  -----------------------------------------------------------------------------------------------
  tls_proxy-https-to-http_1       /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
  tls_proxy-https-to-https_1      /docker-entrypoint.sh /usr ... Up      0.0.0.0:10001->10000/tcp
  tls_proxy-http-to-https_1       /docker-entrypoint.sh /usr ... Up      0.0.0.0:10002->10000/tcp
  tls_proxy-https-passthrough_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:10003->10000/tcp
  tls_service-http_1              node ./index.js                Up
  tls_service-https_1             node ./index.js                Up

Step 4: Test proxying ``https`` -> ``http``
********************************************

The Envoy proxy listening on https://localhost:10000 terminates ``HTTPS`` and proxies to the upstream ``HTTP`` service.

.. code-block:: console

   $ curl -sk https://localhost:10000  | jq  '.headers["x-forwarded-proto"]'
   "https"

   $ curl -sk https://localhost:10000  | jq  '.os.hostname'
   "service-http"

Step 5: Test proxying ``https`` -> ``https``
********************************************

The Envoy proxy listening on https://localhost:10001 terminates ``HTTPS`` and proxies to the upstream ``HTTPS`` service.

.. code-block:: console

   $ curl -sk https://localhost:10001  | jq  '.headers["x-forwarded-proto"]'
   "https"

   $ curl -sk https://localhost:10001  | jq  '.os.hostname'
   "service-https"

Step 6: Test proxying ``http`` -> ``https``
*******************************************

The Envoy proxy listening on http://localhost:10002 terminates ``HTTP`` and proxies to the upstream ``HTTPS`` service.

.. code-block:: console

   $ curl -s http://localhost:10002  | jq  '.headers["x-forwarded-proto"]'
   "http"

   $ curl -s http://localhost:10002  | jq  '.os.hostname'
   "service-https"


Step 7: Test proxying ``https`` passthrough
*******************************************

The Envoy proxy listening on https://localhost:10003 proxies directly to the upstream ``HTTPS`` service which
does the ``TLS`` termination.

.. code-block:: console

   $ curl -sk http://localhost:10003  | jq  '.os.hostname'
   "service-https"
