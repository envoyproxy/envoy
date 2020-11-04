.. _start_quick_start_securing:


Securing Envoy
==============

Envoy provides a number of features to secure traffic in and out of your network, and
between proxies and services within your network.

TLS can be used to secure ``HTTP`` traffic and other ``HTTP``-based traffic, such as WebSockets.

Envoy also has support for terminating other types of TLS traffic (such as ``SMTP``) and for
transmitting generic ``TCP`` over ``TLS`` between proxies.

.. warning::

   The following guide takes you through individual aspects of securing traffic.

   To secure traffic over a network that is untrusted, you are strongly advised to make
   use of SNI *and* mTLS wherever you control both sides of the connection or where these protocols are available.

   The examples presented here use insecure certificates for demonstration purposes.

   It is your responsibility to ensure the integrity of your certificate chain, and outside the scope of this guide.

Upstream and downstream TLS contexts
------------------------------------

Machines connecting to Envoy to proxy traffic are "downstream" to Envoy.

Specifying a TLS context that clients can connect to is done using a ``DownstreamTLSContext``:

.. literalinclude:: _include/envoy-demo-tls.yaml
   :language: yaml
   :linenos:
   :lineno-start: 27
   :lines: 27-34
   :emphasize-lines: 5

Connecting to an upstream TLS service is conversely done with an ``UpstreamTLSContext``:

.. literalinclude:: _include/envoy-demo-tls.yaml
   :language: yaml
   :linenos:
   :lineno-start: 92
   :lines: 92-111
   :emphasize-lines: 16-20

Secure an endpoint with SNI
---------------------------

Connect to an endpoint securely with SNI
----------------------------------------

Validate an endpoint's certificates when connecting
---------------------------------------------------

Use mututal TLS (mTLS) to perform client certificate authentication
-------------------------------------------------------------------
