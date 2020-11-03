.. _start_quick_start_securing:


Securing Envoy
==============

Envoy provides a number of features to secure traffic in and out of your network, and
between proxies and services within your network.

.. warning::

   The following guide takes you through individual aspects of securing traffic.

   To secure traffic over a network that is untrusted, you are strongly advised to make
   use of SNI *and* mTLS wherever you control both endpoints or where these protocols are available.

   The examples presented here use insecure certificates for demonstration purposes.

   It is your responsibility to ensure the integrity of your certificate chain, and outside the scope of this guide.

Upstream and downstream TLS contexts
------------------------------------

Machines connecting to Envoy to proxy traffic are "downstream" to Envoy.

Specifying a TLS context that clients can connect to is done using a ``DownstreamTLSContext``:

.. literalinclude::
   :language: yaml
   :linenos:
   :lines: 27-34
   :emphasize-lines: 5

Connecting to an upstream TLS service is conversely done with an ``UpstreamTLSContext``:

.. literalinclude::
   :language: yaml
   :linenos:
   :lines: 106-111
   :emphasize-lines: 5

Secure an endpoint with SNI
---------------------------

Connect to an endpoint securely with SNI
----------------------------------------

Validate an endpoint's certificates when connecting
---------------------------------------------------

Use mututal TLS (mTLS) to perform client certificate authentication
-------------------------------------------------------------------
