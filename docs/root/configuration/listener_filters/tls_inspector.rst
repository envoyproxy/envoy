.. _config_listener_filters_tls_inspector:

TLS Inspector
=============

TLS inspector listener filter allows detecting whether the transport appears to be
TLS or plaintext, and if it is TLS, it detects the
`server name indication <https://en.wikipedia.org/wiki/Server_Name_Indication>`_
from the client. This can be used to select a
:ref:`FilterChain <envoy_api_msg_listener.FilterChain>` via the
:ref:`sni_domains <envoy_api_field_listener.FilterChainMatch.sni_domains>` of
a :ref:`FilterChainMatch <envoy_api_msg_listener.FilterChainMatch>`.

* :ref:`SNI <faq_how_to_setup_sni>`
* :ref:`v2 API reference <envoy_api_field_listener.ListenerFilter.name>`
