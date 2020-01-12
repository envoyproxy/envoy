.. _config_filters_proxy_protocol:

Proxy Protocol
==============

* :ref:`v2 API reference <envoy_api_msg_config.filter.network.proxy_protocol.v2.ProxyProtocol>`
* This filter should be configured with the name *envoy.filters.network.proxy_protocol*.
* This filter *must* be configured with TCP proxy.

Overview
--------

This network filter adds support for generating
`HAProxy PROXY Protocol <https://www.haproxy.org/download/2.1/doc/proxy-protocol.txt>`_.

In this mode, the downstream connection's remote and local addresses are used to generate
the PROXY protocol header, which is then propagated to the upstream. The upstream receiver
must be able to process this header.

PROXY protocol v1 and v2 are supported. The version to use is selected through filter
configuration. v2 has support for indicating traffic was generated from the proxy itself.
This is used when sending health checks from Envoy. v1 does not have a designated indicator for
this type of traffic and Envoy's local address will be used as the source. Note that this is
innapropriate if any source NAT happens between Envoy and the upstream receiver.

In PROXY Protocol v2 there exists the concept of optional extension (TLV) tags.
This implementation currently does not have support for generating any of these.

Configuration
-------------

The following is a partial configuration example of how to setup the PROXY protocol filter with
TCP proxy:

.. code-block:: yaml

  listeners:
  - filter_chains:
    - filters:
      - name: envoy.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.config.filter.network.tcp_proxy.v2.TcpProxy
          cluster: myservice
          stat_prefix: tcp_proxy_stats
    ...
  clusters:
  - name: myservice
    filters:
    - name: "envoy.filters.network.proxy_protocol"
      typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.proxy_protocol.v3.ProxyProtocol
          version: V1
    ...
