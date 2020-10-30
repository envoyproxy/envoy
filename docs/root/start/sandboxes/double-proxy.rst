.. _install_sandboxes_double_proxy:

Double proxy
============

This sandbox demonstrates a basic "double proxy" configuration, in which a simple ``Flask`` app
connects to a ``PostgreSql`` database, with two Envoy proxies in between.

``Envoy (front)`` -> ``Flask`` -> ``Envoy (postgres-front)`` -> ``Envoy (postgres-back)`` -> ``PostgreSql``

This type of setup is common in a service mesh where Envoy acts as a "sidecar" between individual services.

It can also be useful as a way of providing access for application servers to upstream services or
databases that may be in a different location or subnet.

Another common use case is with Envoy configured to provide a "Point of presence" at the edge of the cloud,
and to relay requests to upstream servers and services.

This example encrypts (and compresses) the transmission of data between the two middle proxies.

This can be useful if the proxies are phsyically separated (or transmit data over untrusted networks).

In order to  use the sandbox you will first need to generate the necessary SSL keys and certificates.

.. include:: _include/docker-env-setup.rst

Change to the ``examples/double-proxy`` directory.

Step 3: Create a certificate authority
**************************************

Step 4: Generate certificates for the proxies
*********************************************

Step 5: Start all of our containers
***********************************

Step 6: Check the flask app can connect to the database
*******************************************************
