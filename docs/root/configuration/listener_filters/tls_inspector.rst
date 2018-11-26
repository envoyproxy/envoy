.. _config_listener_filters_tls_inspector:

TLS Inspector
=============

TLS Inspector listener filter allows detecting whether the transport appears to be
TLS or plaintext, and if it is TLS, it detects the
`Server Name Indication <https://en.wikipedia.org/wiki/Server_Name_Indication>`_
and/or `Application-Layer Protocol Negotiation
<https://en.wikipedia.org/wiki/Application-Layer_Protocol_Negotiation>`_
from the client. This can be used to select a
:ref:`FilterChain <envoy_api_msg_listener.FilterChain>` via the
:ref:`server_names <envoy_api_field_listener.FilterChainMatch.server_names>` and/or
:ref:`application_protocols <envoy_api_field_listener.FilterChainMatch.application_protocols>`
of a :ref:`FilterChainMatch <envoy_api_msg_listener.FilterChainMatch>`.

* This filter should be configured with the name *envoy.listener.proxy_protocol*.
* :ref:`SNI <faq_how_to_setup_sni>`
* :ref:`v2 API reference <envoy_api_field_listener.ListenerFilter.name>`
