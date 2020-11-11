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

To better understand the provided examples, and for a description of how ``TLS`` is
configured with Envoy, please see the :ref:`securing Envoy quick start guide <start_quick_start_securing>`.

The :ref:`double proxy sandbox <install_sandboxes_double_proxy>` also provides an example
of securing traffic between proxies with validation and mutual authentication using ``mTLS``.

.. warning::

   For the sake of simplicity, the examples provided here do not authenticate any client certificates,
   or validate any of the provided certificates.

   When using ``TLS``, you are strongly encouraged to :ref:`validate <start_quick_start_securing_validation>`
   all certificates wherever possible.

.. admonition:: Requirements

   This example makes use of the `jq <https://stedolan.github.io/jq/>`_ tool to parse ``json`` output
   from the upstream echo servers.

.. include:: _include/docker-env-setup.rst

Change directory to ``examples/tls`` in the Envoy repository.

Step 3: Build the sandbox
*************************

This starts four proxies listening on ``localhost`` ports ``10000-10003``.

It also starts two upstream services, one ``HTTP`` and one ``HTTPS``, which echo back received headers
in ``json`` format.

The upstream services listen on the internal Docker network on ports ``80`` and ``443`` respectively.

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

The :download:`provided configuration <_include/tls/envoy-http-https.yaml>` adds a ``TLS`` ``transport_socket`` to
the listener.

Querying the service at port ``10000`` we should see am ``x-forwarded-proto`` header of ``https`` has
been added:

.. code-block:: console

   $ curl -sk https://localhost:10000  | jq  '.headers["x-forwarded-proto"]'
   "https"

The upstream ``http`` service handles the request.

.. code-block:: console

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
