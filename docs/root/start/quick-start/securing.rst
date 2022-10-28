.. _start_quick_start_securing:

Securing Envoy
==============

Envoy provides a number of features to secure traffic in and out of your network, and
between proxies and services within your network.

Transport Layer Security (``TLS``) can be used to secure all types of ``HTTP`` traffic, including ``WebSockets``.

Envoy also has support for transmitting and receiving generic ``TCP`` traffic with ``TLS``.

Envoy also offers a number of other ``HTTP``-based protocols for authentication and authorization
such as :ref:`JWT <arch_overview_jwt_authn>`, :ref:`RBAC <arch_overview_rbac>`
and :ref:`OAuth <envoy_v3_api_file_envoy/extensions/filters/http/oauth2/v3/oauth.proto>`.

.. warning::

   The following guide takes you through individual aspects of securing traffic.

   To secure traffic over a network that is untrusted, you are strongly advised to make use of encryption
   and mutual authentication wherever you control both sides of the connection or where relevant protocols are available.

   Here we provide a guide to using :ref:`mTLS <start_quick_start_securing_mtls>` which provides both encryption
   and mutual authentication.

   When using ``TLS``, you are strongly encouraged to :ref:`validate <start_quick_start_securing_validation>`
   all certificates wherever possible.

   It is your responsibility to ensure the integrity of your certificate chain, and outside the scope of this guide.

.. _start_quick_start_securing_contexts:

Upstream and downstream ``TLS`` contexts
----------------------------------------

Machines connecting to Envoy to proxy traffic are "downstream" in relation to Envoy.

Specifying a ``TLS`` context that clients can connect to is achieved by setting the
:ref:`DownstreamTLSContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.DownstreamTlsContext>`
in the :ref:`transport_socket <extension_envoy.transport_sockets.tls>` of a
:ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>`.

You will also need to provide valid certificates.

.. literalinclude:: _include/envoy-demo-tls.yaml
   :language: yaml
   :linenos:
   :lines: 1-40
   :emphasize-lines: 30-39
   :caption: :download:`envoy-demo-tls.yaml <_include/envoy-demo-tls.yaml>`

Connecting to an "upstream" ``TLS`` service is conversely done by adding an
:ref:`UpstreamTLSContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>`
to the :ref:`transport_socket <extension_envoy.transport_sockets.tls>` of a
:ref:`cluster <envoy_v3_api_msg_config.cluster.v3.Cluster>`.

.. literalinclude:: _include/envoy-demo-tls.yaml
   :language: yaml
   :linenos:
   :lineno-start: 40
   :lines: 40-58
   :emphasize-lines: 15-19
   :caption: :download:`envoy-demo-tls.yaml <_include/envoy-demo-tls.yaml>`

.. _start_quick_start_securing_validation:

Validate an endpoint's certificates when connecting
---------------------------------------------------

When Envoy connects to an upstream ``TLS`` service, it does not, by default, validate the certificates
that it is presented with.

You can use the :ref:`validation_context <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CertificateValidationContext>`
to specify how Envoy should validate these certificates.

Firstly, you can ensure that the certificates are from a mutually trusted certificate authority:

.. literalinclude:: _include/envoy-demo-tls-validation.yaml
   :language: yaml
   :linenos:
   :lineno-start: 42
   :lines: 42-52
   :emphasize-lines: 8-11
   :caption: :download:`envoy-demo-tls-validation.yaml <_include/envoy-demo-tls-validation.yaml>`

You can also ensure that the "Subject Alternative Names" for the cerficate match.

This is commonly used by web certificates (X.509) to identify the domain or domains that a
certificate is valid for.

.. literalinclude:: _include/envoy-demo-tls-validation.yaml
   :language: yaml
   :linenos:
   :lineno-start: 44
   :lines: 44-54
   :emphasize-lines: 6-7, 10-11
   :caption: :download:`envoy-demo-tls-validation.yaml <_include/envoy-demo-tls-validation.yaml>`

.. note::

   If the "Subject Alternative Names" for a certificate are for a wildcard domain, eg ``*.example.com``,
   this is what you should use when matching with ``match_typed_subject_alt_names``.

.. note::

   See :ref:`here <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CertificateValidationContext>` to view all
   of the possible configurations for certificate validation.

.. _start_quick_start_securing_mtls:

Use mutual ``TLS`` (``mTLS``) to enforce client certificate authentication
---------------------------------------------------------------------------

With mutual ``TLS`` (``mTLS``), Envoy also provides a way to authenticate connecting clients.

At a minimum you will need to set
:ref:`require_client_certificate <envoy_v3_api_field_extensions.transport_sockets.tls.v3.DownstreamTlsContext.require_client_certificate>`
and specify a mutually trusted certificate authority:

.. literalinclude:: _include/envoy-demo-tls-client-auth.yaml
   :language: yaml
   :linenos:
   :lineno-start: 29
   :lines: 29-43
   :emphasize-lines: 6, 8-10
   :caption: :download:`envoy-demo-tls-client-auth.yaml <_include/envoy-demo-tls-client-auth.yaml>`

You can further restrict the authentication of connecting clients by specifying the allowed
"Subject Alternative Names" in
:ref:`match_typed_subject_alt_names <envoy_v3_api_field_extensions.transport_sockets.tls.v3.CertificateValidationContext.match_typed_subject_alt_names>`,
similar to validating upstream certificates :ref:`described above <start_quick_start_securing_validation>`.

.. literalinclude:: _include/envoy-demo-tls-client-auth.yaml
   :language: yaml
   :linenos:
   :lineno-start: 29
   :lines: 29-43
   :emphasize-lines: 7, 11-14
   :caption: :download:`envoy-demo-tls-client-auth.yaml <_include/envoy-demo-tls-client-auth.yaml>`

.. note::

   See :ref:`here <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.CertificateValidationContext>` to view all
   of the possible configurations for certificate validation.

.. _start_quick_start_securing_mtls_client:

Use mutual ``TLS`` (``mTLS``) to connect with client certificates
------------------------------------------------------------------

When connecting to an upstream with client certificates you can set them as follows:

.. literalinclude:: _include/envoy-demo-tls-client-auth.yaml
   :language: yaml
   :linenos:
   :lineno-start: 46
   :lines: 46-70
   :emphasize-lines: 22-25
   :caption: :download:`envoy-demo-tls-client-auth.yaml <_include/envoy-demo-tls-client-auth.yaml>`

.. _start_quick_start_securing_sni:

Provide multiple ``TLS`` domains at the same ``IP`` address with ``SNI``
------------------------------------------------------------------------

``SNI`` is an extension to the ``TLS`` protocol which allows multiple domains served
from the same ``IP`` address to be secured with ``TLS``.

To secure specific domains on a listening connection with ``SNI``, you should set the
:ref:`filter_chain_match <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>` of the
:ref:`listener <envoy_v3_api_msg_config.listener.v3.Listener>`:

.. literalinclude:: _include/envoy-demo-tls-sni.yaml
   :language: yaml
   :linenos:
   :lineno-start: 29
   :lines: 29-37
   :emphasize-lines: 2-4
   :caption: :download:`envoy-demo-tls-sni.yaml <_include/envoy-demo-tls-sni.yaml>`

See here for :ref:`more info about creating multiple endpoints with SNI <faq_how_to_setup_sni>`

.. _start_quick_start_securing_sni_client:

Connect to an endpoint with ``SNI``
-----------------------------------

When connecting to a ``TLS`` endpoint that uses ``SNI`` you should set
:ref:`sni <envoy_v3_api_field_extensions.transport_sockets.tls.v3.UpstreamTlsContext.sni>` in the configuration
of the :ref:`UpstreamTLSContext <envoy_v3_api_msg_extensions.transport_sockets.tls.v3.UpstreamTlsContext>`.

This will usually be the DNS name of the service you are connecting to.

.. literalinclude:: _include/envoy-demo-tls-sni.yaml
   :language: yaml
   :linenos:
   :lineno-start: 57
   :lines: 57-62
   :emphasize-lines: 6
   :caption: :download:`envoy-demo-tls-sni.yaml <_include/envoy-demo-tls-sni.yaml>`

When connecting to an Envoy endpoint that is protected by ``SNI``, this must match one of the
:ref:`server_names <envoy_v3_api_field_config.listener.v3.FilterChainMatch.server_names>` set in the endpoint's
:ref:`filter_chain_match <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>`, as
:ref:`described above <start_quick_start_securing_sni>`.
