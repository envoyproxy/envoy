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

   You are also strongly encouraged to validate all certificates wherever possible.

   It is your responsibility to ensure the integrity of your certificate chain, and outside the scope of this guide.

Upstream and downstream TLS contexts
------------------------------------

Machines connecting to Envoy to proxy traffic are "downstream" to Envoy.

Specifying a TLS context that clients can connect to is done using a ``DownstreamTLSContext``:

.. literalinclude:: _include/envoy-demo-tls.yaml
   :language: yaml
   :linenos:
   :lineno-start: 27
   :lines: 27-37
   :emphasize-lines: 5

Connecting to an upstream TLS service is conversely done with an ``UpstreamTLSContext``:

.. literalinclude:: _include/envoy-demo-tls.yaml
   :language: yaml
   :linenos:
   :lineno-start: 39
   :lines: 39-57
   :emphasize-lines: 16-19

Validate an endpoint's certificates when connecting
---------------------------------------------------

When Envoy connects to an upstream TLS service, it does not, by default, validate the certificates
that it is presented with.

You can use the ``validation_context`` to specify how Envoy should validate these certificates.

Firstly, you can ensure that the certificates are from a mutually trusted certificate authority:

.. literalinclude:: _include/envoy-demo-tls-validation.yaml
   :language: yaml
   :linenos:
   :lineno-start: 43
   :lines: 43-53
   :emphasize-lines: 6-9

You can also ensure that the "Subject Alternative Names" for the cerficate match.

This is commonly used by web certificates (X.509) to identify the domain or domains that a
certificate is valid for.

.. literalinclude:: _include/envoy-demo-tls-validation.yaml
   :language: yaml
   :linenos:
   :lineno-start: 43
   :lines: 43-53
   :emphasize-lines: 6-7, 10-11

.. note::

   If the "Subject Alternative Names" for a certificate are for a wildcard domain, eg ``*.example.com``,
   this is what you should use when matching with ``match_subject_alt_names``.


Secure an endpoint with SNI
---------------------------

SNI is an extension to the TLS protocol through which a client indicates the hostname of the server
that they are connecting to during the TLS negotiation.

To enforce SNI on a listening connection, you should set the ``filter_chain_match`` of the ``listener``:

.. literalinclude:: _include/envoy-demo-tls-sni.yaml
   :language: yaml
   :linenos:
   :lineno-start: 27
   :lines: 27-35
   :emphasize-lines: 2-4

Connect to an endpoint securely with SNI
----------------------------------------

When connecting to a TLS endpoint that is protected by SNI you can set ``sni`` in the configuration
of the ``UpstreamTLSContext``.

.. literalinclude:: _include/envoy-demo-tls-sni.yaml
   :language: yaml
   :linenos:
   :lineno-start: 56
   :lines: 56-61
   :emphasize-lines: 6

When connecting to an Envoy endpoint that is protected by SNI, this must match exactly one of the
``server_names`` set in the endpoints ``filter_chain_match``.

When connecting to an endpoint that is not protected by SNI, this configuration is ignored, so it is
generally advisable to always set this to the DNS name of the endpoint you are connecting to.

Use mututal TLS (mTLS) to enforce client certificate authentication
-------------------------------------------------------------------

.. literalinclude:: _include/envoy-demo-tls-client-auth.yaml
   :language: yaml
   :linenos:
   :lineno-start: 27
   :lines: 27-39
   :emphasize-lines: 6, 8-10


.. literalinclude:: _include/envoy-demo-tls-client-auth.yaml
   :language: yaml
   :linenos:
   :lineno-start: 27
   :lines: 27-39
   :emphasize-lines: 7, 11-12


Use mututal TLS (mTLS) to connect with client certificates
----------------------------------------------------------

.. literalinclude:: _include/envoy-demo-tls-client-auth.yaml
   :language: yaml
   :linenos:
   :lineno-start: 45
   :lines: 45-74
   :emphasize-lines: 21-28
