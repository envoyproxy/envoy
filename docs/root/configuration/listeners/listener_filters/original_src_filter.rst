.. _config_listener_filters_original_src:

Original Source
===============

* :ref:`Listener filter v3 API reference <envoy_v3_api_msg_extensions.filters.listener.original_src.v3.OriginalSrc>`
* This filter should be configured with the name *envoy.filters.listener.original_src*.

The original source listener filter replicates the downstream remote address of the connection on
the upstream side of Envoy. For example, if a downstream connection connects to Envoy with IP
address ``10.1.2.3``, then Envoy will connect to the upstream with source IP ``10.1.2.3``.

Interaction with Proxy Protocol
--------------------------------

If the connection has not had its source address translated or proxied, then Envoy can simply use
the existing connection information to build the correct downstream remote address. However, if this
is not true, a :ref:`Proxy Protocol filter <config_listener_filters_proxy_protocol>` may be used to
extract the downstream remote address.

IP Version Support
------------------
The filter supports both IPv4 and IPv6 as addresses. Note that the upstream connection must support
the version used.

Extra Setup
-----------

The downstream remote address used will likely be globally routable. By default, packets returning
from the upstream host to that address will not route through Envoy. The network must be configured
to forcefully route any traffic whose IP was replicated by Envoy back through the Envoy host.

If Envoy and the upstream are on the same host -- e.g. in an sidecar deployment --, then iptables
and routing rules can be used to ensure correct behaviour. The filter has an unsigned integer
configuration,
:ref:`mark <envoy_v3_api_field_extensions.filters.listener.original_src.v3.OriginalSrc.mark>`. Setting
this to *X* causes Envoy to *mark* all upstream packets originating from this listener with value
*X*. Note that if
:ref:`mark <envoy_v3_api_field_extensions.filters.listener.original_src.v3.OriginalSrc.mark>` is set
to 0, Envoy will not mark upstream packets.

We can use the following set of commands to ensure that all ipv4 and ipv6 traffic marked with *X*
(assumed to be 123 in the example) routes correctly. Note that this example assumes that *eth0* is
the default outbound interface.

.. code-block:: text

  iptables  -t mangle -I PREROUTING -m mark     --mark 123 -j CONNMARK --save-mark
  iptables  -t mangle -I OUTPUT     -m connmark --mark 123 -j CONNMARK --restore-mark
  ip6tables -t mangle -I PREROUTING -m mark     --mark 123 -j CONNMARK --save-mark
  ip6tables -t mangle -I OUTPUT     -m connmark --mark 123 -j CONNMARK --restore-mark
  ip rule add fwmark 123 lookup 100
  ip route add local 0.0.0.0/0 dev lo table 100
  ip -6 rule add fwmark 123 lookup 100
  ip -6 route add local ::/0 dev lo table 100
  echo 1 > /proc/sys/net/ipv4/conf/eth0/route_localnet


Example Listener configuration
------------------------------

The following example configures Envoy to use the original source for all connections made on port
8888. It uses Proxy Protocol to determine the downstream remote address. All upstream packets are
marked with 123.

.. code-block:: yaml

  listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 8888
    listener_filters:
      - name: envoy.filters.listener.proxy_protocol
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol
      - name: envoy.filters.listener.original_src
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.original_src.v3.OriginalSrc
          mark: 123
