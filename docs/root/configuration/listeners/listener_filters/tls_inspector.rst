.. _config_listener_filters_tls_inspector:

TLS Inspector
=============

TLS Inspector listener filter allows detecting whether the transport appears to be
TLS or plaintext, and if it is TLS, it detects the
`Server Name Indication <https://en.wikipedia.org/wiki/Server_Name_Indication>`_
and/or `Application-Layer Protocol Negotiation
<https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_
from the client. This can be used to select a
:ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` via the
:ref:`server_names <envoy_v3_api_field_config.listener.v3.FilterChainMatch.server_names>` and/or
:ref:`application_protocols <envoy_v3_api_field_config.listener.v3.FilterChainMatch.application_protocols>`
of a :ref:`FilterChainMatch <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>`.

* :ref:`SNI <faq_how_to_setup_sni>`
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.listener.tls_inspector.v3.TlsInspector>`

Example
-------

A sample filter configuration could be:

.. code-block:: yaml

  listener_filters:
  - name: tls_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector

Statistics
----------

This filter has a statistics tree rooted at *tls_inspector* with the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  client_hello_too_large, Counter, Total unreasonably large Client Hello received
  tls_found, Counter, Total number of times TLS was found
  tls_not_found, Counter, Total number of times TLS was not found
  alpn_found, Counter, Total number of times `Application-Layer Protocol Negotiation <https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_ was successful
  alpn_not_found, Counter, Total number of times `Application-Layer Protocol Negotiation <https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_ has failed
  sni_found, Counter, Total number of times `Server Name Indication <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ was found
  sni_not_found, Counter, Total number of times `Server Name Indication <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ was not found

