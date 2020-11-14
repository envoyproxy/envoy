.. _install_sandboxes_tls_sni:

Server name indication (``TLS``/``SNI``)
========================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
	Used to make ``HTTP`` requests.

   :ref:`jq <start_sandboxes_setup_jq>`
	Parse ``json`` output from the upstream echo servers.

This example demonstrates a server that listens on multiple domains
and provides separate ``TLS`` termination for each.

Step 1: Create keypairs for each of the domain endpoints
********************************************************

Change directory to ``examples/tls-sni`` in the Envoy repository.

This example creates three ``TLS`` endpoints and each will require their own
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

   $ openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
	    -subj "/C=US/ST=CA/O=MyExample, Inc./CN=domain3.example.com" \
	    -keyout certs/domain3.key.pem \
	    -out certs/domain3.crt.pem
   Generating a RSA private key
   .............+++++
   ...................+++++
   writing new private key to 'certs/domain3.key.pem'
   -----

Step 2: Start the containers
****************************

Build and start the containers.

This will load the required keys and certificates you created into the Envoy proxy container.

.. code-block:: console

   $ pwd
   envoy/examples/tls-sni
   $ docker-compose build --pull
   $ docker-compose up -d
   $ docker-compose ps

          Name                                 Command                State         Ports
   ---------------------------------------------------------------------------------------------------
   tls-sni_proxy_1                     /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
   tls-sni_service-http_1              node ./index.js                Up

Step 2: Query the ``SNI`` endpoints directly with curl
******************************************************

asdf


Step 2: Query the ``SNI`` endpoints via an Envoy proxy client
*************************************************************

asdf

.. seealso::

   :ref:`Securing Envoy quick start guide <start_quick_start_securing>`
      Outline of key concepts for securing Envoy.

   :ref:`TLS sandbox <install_sandboxes_tls>`
      Sandbox featuring examples of how Envoy can be configured to make
      use of encrypted connections using ``HTTP`` over ``TLS``.

   :ref:`Double proxy sandbox <install_sandboxes_double_proxy>`
      An example of securing traffic between proxies with validation and
      mutual authentication using ``mTLS`` with non-``HTTP`` traffic.
