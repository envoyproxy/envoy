.. _install_sandboxes_tls_sni:

TLS Server name indication (``SNI``)
====================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

   :ref:`jq <start_sandboxes_setup_jq>`
        Parse ``json`` output from the upstream echo servers.

This example demonstrates an Envoy proxy that listens on three ``TLS`` domains
on the same ``IP`` address.

The first two domains (``domain1`` and ``domain2``) terminate the ``TLS`` and proxy
to upstream ``HTTP`` hosts.

The other domain (``domain3``) is proxied unterminated, based on the ``SNI`` headers.

It also demonstrates Envoy acting as a client proxy connecting to upstream ``SNI`` services.

.. _install_sandboxes_tls_sni_step1:

Step 1: Create keypairs for each of the domain endpoints
********************************************************

Change directory to ``examples/tls-sni`` in the Envoy repository.

The example creates two Envoy ``TLS`` endpoints and they will require their own
keypairs.

Create self-signed certificates for these endpoints as follows:

.. code-block:: console

   $ pwd
   envoy/examples/tls-sni

   $ mkdir -p certs

   $ openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
            -subj "/C=US/ST=CA/O=MyExample, Inc./CN=domain1.example.com" \
            -keyout certs/domain1.key.pem \
            -out certs/domain1.crt.pem
   Generating a RSA private key
   .............+++++
   ...................+++++
   writing new private key to 'certs/domain1.key.pem'
   -----

   $ openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
            -subj "/C=US/ST=CA/O=MyExample, Inc./CN=domain2.example.com" \
            -keyout certs/domain2.key.pem \
            -out certs/domain2.crt.pem
   Generating a RSA private key
   .............+++++
   ...................+++++
   writing new private key to 'certs/domain2.key.pem'
   -----

.. warning::

   ``SNI`` does *not* validate that the certificates presented are correct for the domain, or that they
   were issued by a recognised certificate authority.

   See the :ref:`Securing Envoy quick start guide <start_quick_start_securing>` for more information about
   :ref:`validating cerfificates <start_quick_start_securing_validation>`.

.. _install_sandboxes_tls_sni_step2:

Step 2: Start the containers
****************************

Build and start the containers.

This starts two upstream ``HTTP`` containers listening on the internal Docker network on port ``80``, and
an upstream ``HTTPS`` service listening on internal port ``443``

In front of these is an Envoy proxy that listens on https://localhost:10000 and serves three ``SNI`` routed
``TLS`` domains:

- ``domain1.example.com``
- ``domain2.example.com``
- ``domain3.example.com``

The first two domains use the keys and certificates :ref:`you created in step 1 <install_sandboxes_tls_sni_step1>` to  terminate ``TLS`` and
proxy to the two upstream ``HTTP`` servers.

The third domain proxies to the upstream ``TLS`` server based on the requested ``SNI`` address, but does no ``TLS`` termination itself.

The composition also starts an Envoy proxy client which listens on http://localhost:20000.

The client proxy has no ``TLS`` termination but instead proxies three routed paths -
``/domain1``, ``/domain2`` and ``/domain3`` - to the ``SNI``-enabled proxy.

.. code-block:: console

   $ pwd
   envoy/examples/tls-sni
   $ docker-compose pull
   $ docker-compose up --build -d
   $ docker-compose ps

          Name                        Command                State         Ports
   -------------------------------------------------------------------------------------------
   tls-sni_http-upstream1_1   node ./index.js                Up
   tls-sni_http-upstream2_1   node ./index.js                Up
   tls-sni_http-upstream3_1   node ./index.js                Up
   tls-sni_proxy_1            /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
   tls-sni_proxy-client_1     /docker-entrypoint.sh /usr ... Up      0.0.0.0:20000->10000/tcp

Step 2: Query the ``SNI`` endpoints directly with curl
******************************************************

You can use curl to query the ``SNI``-routed ``HTTPS`` endpoints of the Envoy proxy directly.

To do this you must explicitly tell curl to resolve the ``DNS`` for the endpoints correctly.

Each endpoint should proxy to the respective ``http-upstream`` or ``https-upstream`` service.

.. code-block:: console

   $ curl -sk --resolve domain1.example.com:10000:127.0.0.1 \
         https://domain1.example.com:10000 \
        | jq -r '.os.hostname'
   http-upstream1

   $ curl -sk --resolve domain2.example.com:10000:127.0.0.1 \
         https://domain2.example.com:10000 \
        | jq -r '.os.hostname'
   http-upstream2

   $ curl -sk --resolve domain3.example.com:10000:127.0.0.1 \
         https://domain3.example.com:10000 \
        | jq -r '.os.hostname'
   https-upstream3

Step 3: Query the ``SNI`` endpoints via an Envoy proxy client
*************************************************************

Next, query the Envoy proxy client using the routed paths.

These route via the ``SNI`` proxy endpoints to the respective ``http-upstream`` or
``https-upstream`` services.

.. code-block:: console

   $ curl -s http://localhost:20000/domain1 \
        | jq '.os.hostname'
   http-upstream1

   $ curl -s http://localhost:20000/domain2 \
        | jq '.os.hostname'
   http-upstream2

   $ curl -s http://localhost:20000/domain3 \
        | jq '.os.hostname'
   https-upstream3

.. seealso::

   :ref:`Securing Envoy quick start guide <start_quick_start_securing>`
      Outline of key concepts for securing Envoy.

   :ref:`TLS sandbox <install_sandboxes_tls>`
      Sandbox featuring examples of how Envoy can be configured to make
      use of encrypted connections using ``HTTP`` over ``TLS``.

   :ref:`Double proxy sandbox <install_sandboxes_double_proxy>`
      An example of securing traffic between proxies with validation and
      mutual authentication using ``mTLS`` with non-``HTTP`` traffic.
