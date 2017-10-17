.. _arch_overview_ssl:

TLS
===

Envoy supports both :ref:`TLS termination <config_listener_ssl_context>` in listeners as well as
:ref:`TLS origination <config_cluster_manager_cluster_ssl>` when making connections to upstream
clusters. Support is sufficient for Envoy to perform standard edge proxy duties for modern web
services as well as to initiate connections with external services that have advanced TLS
requirements (TLS1.2, SNI, etc.). Envoy supports the following TLS features:

* **Configurable ciphers**: Each TLS listener and client can specify the ciphers that it supports.
* **Client certificates**: Upstream/client connections can present a client certificate in addition
  to server certificate verification.
* **Certificate verification and pinning**: Certificate verification options include basic chain
  verification, subject name verification, and hash pinning.
* **ALPN**: TLS listeners support ALPN. The HTTP connection manager uses this information (in
  addition to protocol inference) to determine whether a client is speaking HTTP/1.1 or HTTP/2.
* **SNI**: SNI is currently supported for client connections. Listener support is likely to be added
  in the future.
* **Session resumption**: Server connections support resuming previous sessions via TLS session
  tickets (see `RFC 5077 <https://www.ietf.org/rfc/rfc5077.txt>`_).  Resumption can be performed
  across hot restarts and between parallel Envoy instances (typically useful in a front proxy
  configuration).

Underlying implementation
-------------------------

Currently Envoy is written to use `BoringSSL <https://boringssl.googlesource.com/boringssl>`_ as the
TLS provider.

.. _arch_overview_ssl_auth_filter:

Authentication filter
---------------------

Envoy provides a network filter that performs TLS client authentication via principals fetched from
a REST VPN service. This filter matches the presented client certificate hash against the principal
list to determine whether the connection should be allowed or not. Optional IP white listing can
also be configured. This functionality can be used to build edge proxy VPN support for web
infrastructure.

Client TLS authentication filter :ref:`configuration reference
<config_network_filters_client_ssl_auth>`.
