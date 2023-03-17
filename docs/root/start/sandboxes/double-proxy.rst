.. _install_sandboxes_double_proxy:

Double proxy (with ``mTLS`` encryption)
=======================================

.. sidebar:: Requirements

   .. include:: _include/docker-env-setup-link.rst

   :ref:`curl <start_sandboxes_setup_curl>`
        Used to make ``HTTP`` requests.

   :ref:`openssl <start_sandboxes_setup_openssl>`
        Generate ``SSL`` keys and certificates.

This sandbox demonstrates a basic "double proxy" configuration, in which a simple Flask app
connects to a PostgreSQL database, with two Envoy proxies in between.

``Envoy (front)`` -> ``Flask`` -> ``Envoy (postgres-front)`` -> ``Envoy (postgres-back)`` -> ``PostgreSQL``

This type of setup is common in a service mesh where Envoy acts as a "sidecar" between individual services.

It can also be useful as a way of providing access for application servers to upstream services or
databases that may be in a different location or subnet, outside of a service mesh or sidecar-based setup.

Another common use case is with Envoy configured to provide "Points of presence" at the edges of the cloud,
and to relay requests to upstream servers and services.

This example encrypts the transmission of data between the two middle proxies and provides mutual authentication
using ``mTLS``.

This can be useful if the proxies are physically separated or transmit data over untrusted networks.

In order to  use the sandbox you will first need to generate the necessary ``SSL`` keys and certificates.

This example walks through creating a certificate authority, and using it to create a domain key and sign
certificates for the proxies.

Change to the ``examples/double-proxy`` directory.

Step 1: Create a certificate authority
**************************************

First create a key for the certificate authority:

.. code-block:: console

   $ pwd
   envoy/examples/double-proxy
   $ mkdir -p certs
   $ openssl genrsa -out certs/ca.key 4096
   Generating RSA private key, 4096 bit long modulus (2 primes)
   ..........++++
   ..........................................................................................................++++
   e is 65537 (0x010001)

Now use the key to generate a certificate authority certificate.

If you wish, you can interactively alter the fields in the certificate.

For the purpose of this example, the defaults should be sufficient.

.. code-block:: console

   $ openssl req -x509 -new -nodes -key certs/ca.key -sha256 -days 1024 -out certs/ca.crt

   You are about to be asked to enter information that will be incorporated
   into your certificate request.
   What you are about to enter is what is called a Distinguished Name or a DN.
   There are quite a few fields but you can leave some blank
   For some fields there will be a default value,
   If you enter '.', the field will be left blank.
   -----
   Country Name (2 letter code) [AU]:
   State or Province Name (full name) [Some-State]:
   Locality Name (eg, city) []:
   Organization Name (eg, company) [Internet Widgits Pty Ltd]:
   Organizational Unit Name (eg, section) []:
   Common Name (e.g. server FQDN or YOUR name) []:
   Email Address []:

Step 2: Create a domain key
***************************

Create a key for the example domain:

.. code-block:: console

   $ openssl genrsa -out certs/example.com.key 2048
   Generating RSA private key, 2048 bit long modulus (2 primes)
   ..+++++
   .................................................+++++
   e is 65537 (0x010001)

Step 3: Generate certificate signing requests for the proxies
*************************************************************

Use the domain key to generate certificate signing requests for each of the proxies:

.. code-block:: console

   $ openssl req -new -sha256 \
        -key certs/example.com.key \
        -subj "/C=US/ST=CA/O=MyExample, Inc./CN=proxy-postgres-frontend.example.com" \
        -out certs/proxy-postgres-frontend.example.com.csr
   $ openssl req -new -sha256 \
        -key certs/example.com.key \
        -subj "/C=US/ST=CA/O=MyExample, Inc./CN=proxy-postgres-backend.example.com" \
        -out certs/proxy-postgres-backend.example.com.csr

Step 4: Sign the proxy certificates
***********************************

You can now use the certificate authority that you created to sign the certificate requests.

Note the ``subjectAltName``. This is used for reciprocally matching and validating the certificates.

.. code-block:: console

   $ openssl x509 -req \
        -in certs/proxy-postgres-frontend.example.com.csr \
        -CA certs/ca.crt \
        -CAkey certs/ca.key \
        -CAcreateserial \
        -extfile <(printf "subjectAltName=DNS:proxy-postgres-frontend.example.com") \
        -out certs/postgres-frontend.example.com.crt \
        -days 500 \
        -sha256
   Signature ok
   subject=C = US, ST = CA, O = "MyExample, Inc.", CN = proxy-postgres-frontend.example.com
   Getting CA Private Key

   $ openssl x509 -req \
        -in certs/proxy-postgres-backend.example.com.csr \
        -CA certs/ca.crt \
        -CAkey certs/ca.key \
        -CAcreateserial \
        -extfile <(printf "subjectAltName=DNS:proxy-postgres-backend.example.com") \
        -out certs/postgres-backend.example.com.crt \
        -days 500 \
        -sha256
   Signature ok
   subject=C = US, ST = CA, O = "MyExample, Inc.", CN = proxy-postgres-backend.example.com
   Getting CA Private Key

At this point you should have the necessary keys and certificates to secure the connection between
the proxies.

The keys and certificates are stored in the ``certs/`` directory.

Step 5: Start all of our containers
***********************************

Build and start the containers.

This will load the required keys and certificates into the frontend and backend proxies.

.. code-block:: console

   $ pwd
   envoy/examples/double-proxy
   $ docker-compose pull
   $ docker-compose up --build -d
   $ docker-compose ps

          Name                                      Command                State         Ports
   --------------------------------------------------------------------------------------------------------
   double-proxy_app_1                       python3 /code/service.py       Up
   double-proxy_postgres_1                  docker-entrypoint.sh postgres  Up      5432/tcp
   double-proxy_proxy-frontend_1            /docker-entrypoint.sh /usr ... Up      0.0.0.0:10000->10000/tcp
   double-proxy_proxy-postgres-backend_1    /docker-entrypoint.sh /usr ... Up      10000/tcp
   double-proxy_proxy-postgres-frontend_1   /docker-entrypoint.sh /usr ... Up      10000/tcp

Step 6: Check the flask app can connect to the database
*******************************************************

Checking the response at http://localhost:10000, you should see the output from the Flask app:

.. code-block:: console

   $ curl -s http://localhost:10000
   Connected to Postgres, version: PostgreSQL 13.0 (Debian 13.0-1.pgdg100+1) on x86_64-pc-linux-gnu, compiled by gcc (Debian 8.3.0-6) 8.3.0, 64-bit

.. seealso::

   :ref:`Securing Envoy quick start guide <start_quick_start_securing>`
      Outline of key concepts for securing Envoy.

   :ref:`TLS sandbox <install_sandboxes_tls>`
      Examples of various ``TLS`` termination patterns with Envoy.
