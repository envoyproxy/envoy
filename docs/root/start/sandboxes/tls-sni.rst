.. _install_sandboxes_tls:

TLS (SNI)
=========


This example demonstrates a server that listens on multiple domains
and provides separate ``TLS`` termination for each.

It might also include a client example...

.. include:: _include/docker-env-setup.rst

Change directory to ``examples/tls-sni`` in the Envoy repository.

Step 3: Create keypairs for each of the domain endpoints
********************************************************

This example creates three ``TLS`` endpoints and each will require their own
keypairs.

Create self-signed certificates for these endpoints as follows:

.. code-block:: console

   $ pwd
   envoy/examples/tls-sni

   $ mkdir -p certs

   $ openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
	    -subj "/C=US/ST=CA/O=MyExample, Inc./CN=domain1.example.com" \
	    -keyout certs/domain1.key \
	    -out certs/domain1.crt
   Generating a RSA private key
   .............+++++
   ...................+++++
   writing new private key to 'certs/domain1.key'
   -----

   $ openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
	    -subj "/C=US/ST=CA/O=MyExample, Inc./CN=domain2.example.com" \
	    -keyout certs/domain2.key \
	    -out certs/domain2.crt
   Generating a RSA private key
   .............+++++
   ...................+++++
   writing new private key to 'certs/domain2.key'
   -----

   $ openssl req -new -newkey rsa:2048 -days 365 -nodes -x509 \
	    -subj "/C=US/ST=CA/O=MyExample, Inc./CN=domain3.example.com" \
	    -keyout certs/domain3.key \
	    -out certs/domain3.crt
   Generating a RSA private key
   .............+++++
   ...................+++++
   writing new private key to 'certs/domain3.key'
   -----

Step 4: Start the containers
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
