.. _install_sandboxes_tls:

Transport layer security (``TLS``)
==================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

   :ref:`jq <start_sandboxes_setup_jq>`
        Parse ``json`` output from the upstream echo servers.

This example walks through some of the ways that Envoy can be configured to make
use of encrypted connections using ``HTTP`` over ``TLS``.

It demonstrates a number of commonly used proxying and ``TLS`` termination patterns:

- ``https`` -> ``http``
- ``https`` -> ``https``
- ``http`` -> ``https``
- ``https`` passthrough

To better understand the provided examples, and for a description of how ``TLS`` is
configured with Envoy, please see the :ref:`securing Envoy quick start guide <start_quick_start_securing>`.

.. warning::

   For the sake of simplicity, the examples provided here do not authenticate any client certificates,
   or validate any of the provided certificates.

   When using ``TLS``, you are strongly encouraged to :ref:`validate <start_quick_start_securing_validation>`
   all certificates wherever possible.

   You should also :ref:`authenticate clients <start_quick_start_securing_mtls>`
   where you control both sides of the connection, or relevant protocols are available.

Step 1: Build the sandbox
*************************

Change directory to ``examples/tls`` in the Envoy repository.

This starts four proxies listening on ``localhost`` ports ``10000-10003``.

It also starts two upstream services, one ``HTTP`` and one ``HTTPS``, which echo back received headers
in ``json`` format.

The upstream services listen on the internal Docker network on ports ``80`` and ``443`` respectively.

.. code-block:: console

  $ pwd
  envoy/examples/tls
  $ docker compose pull
  $ docker compose up --build -d
  $ docker compose ps

         Name                            Command                 State          Ports
  -----------------------------------------------------------------------------------------------
  tls_proxy-https-to-http_1       /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
  tls_proxy-https-to-https_1      /docker-entrypoint.sh /usr ... Up      0.0.0.0:10001->10000/tcp
  tls_proxy-http-to-https_1       /docker-entrypoint.sh /usr ... Up      0.0.0.0:10002->10000/tcp
  tls_proxy-https-passthrough_1   /docker-entrypoint.sh /usr ... Up      0.0.0.0:10003->10000/tcp
  tls_service-http_1              node ./index.js                Up
  tls_service-https_1             node ./index.js                Up

Step 2: Test proxying ``https`` -> ``http``
*******************************************

The Envoy proxy listening on https://localhost:10000 terminates ``HTTPS`` and proxies to the upstream ``HTTP`` service.

The :download:`https -> http configuration <_include/tls/envoy-https-http.yaml>` adds a ``TLS``
:ref:`transport_socket <extension_envoy.transport_sockets.tls>` to the
:ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>`.

Querying the service at port ``10000`` you should see an ``x-forwarded-proto`` header of ``https`` has
been added:

.. code-block:: console

   $ curl -sk https://localhost:10000  | jq -r '.headers["x-forwarded-proto"]'
   https

The upstream ``service-http`` handles the request.

.. code-block:: console

   $ curl -sk https://localhost:10000  | jq -r '.os.hostname'
   service-http

Step 3: Test proxying ``https`` -> ``https``
********************************************

The Envoy proxy listening on https://localhost:10001 terminates ``HTTPS`` and proxies to the upstream ``HTTPS`` service.

The :download:`https -> https configuration <_include/tls/envoy-https-https.yaml>` adds a ``TLS``
:ref:`transport_socket <extension_envoy.transport_sockets.tls>` to both the
:ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>` and the
:ref:`cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>`.

Querying the service at port ``10001`` you should see an ``x-forwarded-proto`` header of ``https`` has
been added:

.. code-block:: console

   $ curl -sk https://localhost:10001  | jq -r '.headers["x-forwarded-proto"]'
   https

The upstream ``service-https`` handles the request.

.. code-block:: console

   $ curl -sk https://localhost:10001  | jq -r '.os.hostname'
   service-https

Step 4: Test proxying ``http`` -> ``https``
*******************************************

The Envoy proxy listening on http://localhost:10002 terminates ``HTTP`` and proxies to the upstream ``HTTPS`` service.

The :download:`http -> https configuration <_include/tls/envoy-http-https.yaml>` adds a ``TLS``
:ref:`transport_socket <extension_envoy.transport_sockets.tls>` to the
:ref:`cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>`.

Querying the service at port ``10002`` you should see an ``x-forwarded-proto`` header of ``http`` has
been added:

.. code-block:: console

   $ curl -s http://localhost:10002  | jq -r '.headers["x-forwarded-proto"]'
   http

The upstream ``service-https`` handles the request.

.. code-block:: console

   $ curl -s http://localhost:10002  | jq -r '.os.hostname'
   service-https


Step 5: Test proxying ``https`` passthrough
*******************************************

The Envoy proxy listening on https://localhost:10003 proxies directly to the upstream ``HTTPS`` service which
does the ``TLS`` termination.

The :download:`https passthrough configuration <_include/tls/envoy-https-passthrough.yaml>` requires no ``TLS``
or ``HTTP`` setup, and instead uses a simple
:ref:`tcp_proxy  <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`.

Querying the service at port ``10003`` you should see that no ``x-forwarded-proto`` header has been
added:

.. code-block:: console

   $ curl -sk https://localhost:10003  | jq -r '.headers["x-forwarded-proto"]'
   null

The upstream ``service-https`` handles the request.

.. code-block:: console

   $ curl -sk https://localhost:10003  | jq -r '.os.hostname'
   service-https

.. seealso::

   :ref:`Securing Envoy quick start guide <start_quick_start_securing>`
      Outline of key concepts for securing Envoy.

   :ref:`TLS SNI sandbox <install_sandboxes_tls_sni>`
      Example of using Envoy to serve multiple domains protected by TLS and
      served from the same ``IP`` address.

   :ref:`Double proxy sandbox <install_sandboxes_double_proxy>`
      An example of securing traffic between proxies with validation and
      mutual authentication using ``mTLS`` with non-``HTTP`` traffic.
