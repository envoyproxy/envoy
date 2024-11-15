.. _config_network_filters_sni_dynamic_forward_proxy:

SNI dynamic forward proxy
=========================

.. attention::

  SNI dynamic forward proxy support should be considered alpha and not production ready.

Through the combination of :ref:`TLS inspector <config_listener_filters_tls_inspector>` listener filter,
this network filter and the
:ref:`dynamic forward proxy cluster <envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`,
Envoy supports `Server Name Indication <https://en.wikipedia.org/wiki/Server_Name_Indication>`_ (SNI) based dynamic forward proxy. The implementation works just like the
:ref:`HTTP dynamic forward proxy <arch_overview_http_dynamic_forward_proxy>`, but using the value in
SNI as target host instead.

The following is a complete configuration that configures both this filter
as well as the :ref:`dynamic forward proxy cluster
<envoy_v3_api_msg_extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig>`. Both filter and cluster
must be configured together and point to the same DNS cache parameters for Envoy to operate as an
SNI dynamic forward proxy.

.. note::

  The following config doesn't terminate TLS in listener, so there is no need to configure TLS context
  in cluster. The TLS handshake is passed through by Envoy.

.. literalinclude:: _include/sni-dynamic-forward-proxy-filter.yaml
    :language: yaml

Dynamic host and port selection
-------------------------------

By default, the SNI dynamic forward proxy uses the SNI as target host, but it can be
dynamically set by other network filters on a per-connection basis by setting a per-connection
state object under the key ``envoy.upstream.dynamic_host``. Additionally, the SNI dynamic forward
proxy uses the default port filter configuration as target port, but it can by dynamically set,
by setting a per-connection state object under the key ``envoy.upstream.dynamic_port``. If these
objects are set, they take precedence over the SNI value and default port. In case that the overrided
port is out of the valid port range, the overriding value will be ignored and the default port
configured will be used. See the implementation for the details.
