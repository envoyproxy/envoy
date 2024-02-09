.. _arch_overview_listener_filters:

Listener filters
================

Envoy's :ref:`listener filters <envoy_v3_api_msg_config.listener.v3.ListenerFilter>`
may be used to manipulate connection metadata.

The main purpose of :ref:`listener filters <envoy_v3_api_msg_config.listener.v3.ListenerFilter>`
are to make adding further system integration functions easier by not requiring changes
to Envoy core functionality, and also to make interaction between multiple such features more
explicit.

The API for :ref:`listener filters <envoy_v3_api_msg_config.listener.v3.ListenerFilter>` is relatively
simple since ultimately these filters operate on newly accepted sockets.

Filters in the chain can stop and subsequently continue iteration to further filters. This allows
for more complex scenarios such as calling a :ref:`rate limiting service <arch_overview_global_rate_limit>`,
etc.

Envoy includes several listener filters that are documented in this architecture overview
as well as the :ref:`configuration reference <config_listener_filters>`.

.. _arch_overview_filter_chains:

Filter chains
~~~~~~~~~~~~~

Filter chain match
------------------

Network filters are :ref:`chained <envoy_v3_api_field_config.listener.v3.Listener.filter_chains>`
in an ordered list of :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>`.

Each listener can have multiple :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` and an optional
:ref:`default_filter_chain <envoy_v3_api_field_config.listener.v3.Listener.default_filter_chain>`.

Upon receiving a request, the :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` with the
most specific :ref:`match <envoy_v3_api_msg_.xds.type.matcher.v3.Matcher>` criteria is used.

If no matching :ref:`FilterChain <envoy_v3_api_msg_config.listener.v3.FilterChain>` is found, the default
filter chain will be used to serve the request, where configured, otherwise the connection will be closed.

.. _filter_chain_only_update:

Filter chain only update
------------------------

:ref:`Filter chains <envoy_v3_api_msg_config.listener.v3.FilterChain>` can be updated independently.

Upon listener config update, if the listener manager determines that the listener update is a filter chain
only update, the listener update will be executed by adding, updating and removing filter chains.

The connections owned by these destroying filter chains will be drained as described :ref:`here <arch_overview_draining>`.

If the new :ref:`filter chain <envoy_v3_api_msg_config.listener.v3.FilterChain>` and the old :ref:`filter chain <envoy_v3_api_msg_config.listener.v3.FilterChain>`
is protobuf message equivalent, the corresponding filter chain runtime info survives. The connections owned by the
survived filter chains remain open.

Not all the listener config updates can be executed by filter chain update. For example, if the listener metadata is
updated within the new listener config, the new metadata must be picked up by the new filter chains. In this case, the
entire listener is drained and updated.

.. _arch_overview_network_filters:

Network (L3/L4) filters
~~~~~~~~~~~~~~~~~~~~~~~

Network level (L3/L4) filters form the core of Envoy connection handling. The filter API allows for
different sets of filters to be mixed and matched and attached to a given listener. There are three
different types of network filters:

**Read**
    Read filters are invoked when Envoy receives data from a downstream connection.
**Write**
    Write filters are invoked when Envoy is about to send data to a downstream connection.
**Read/Write**
    Read/Write filters are invoked both when Envoy receives data from a downstream
    connection and when it is about to send data to a downstream connection.

The API for network level filters is relatively simple since ultimately the filters operate on raw
bytes and a small number of connection events (e.g., TLS handshake complete, connection disconnected
locally or remotely, etc.).

Filters in the chain can stop and subsequently continue iteration to further filters. This allows
for more complex scenarios such as calling a :ref:`rate limiting service <arch_overview_global_rate_limit>`,
etc.

Network level filters can also share state (static and dynamic) among themselves within the
context of a single downstream connection. Refer to :ref:`data sharing between filters
<arch_overview_data_sharing_between_filters>` for more details.

.. tip::
   See the listener :ref:`configuration <config_network_filters>` and
   :ref:`protobuf <envoy_v3_api_file_envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto>`
   sections for reference documentation.

   See :ref:`here <extension_category_envoy.filters.network>` for included filters.

.. _arch_overview_tcp_proxy:

TCP proxy filter
----------------

The TCP proxy filter performs basic 1:1 network connection proxy between downstream clients and upstream
clusters.

It can be used by itself as an stunnel replacement, or in conjunction with other filters
such as the :ref:`MongoDB filter <arch_overview_mongo>` or the :ref:`rate limit
<config_network_filters_rate_limit>` filter.

The TCP proxy filter will respect the
:ref:`connection limits <envoy_v3_api_field_config.cluster.v3.CircuitBreakers.Thresholds.max_connections>`
imposed by each upstream cluster's global resource manager. The TCP proxy filter checks with the
upstream cluster's resource manager if it can create a connection without going over that cluster's
maximum number of connections, if it can't the TCP proxy will not make the connection.

.. tip::
   See the :ref:`TCP proxy configuration <config_network_filters>` and
   :ref:`protobuf <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`
   sections for reference documentation.

.. _arch_overview_udp_proxy:

UDP proxy filter
----------------

Envoy supports UDP proxy via the :ref:`UDP proxy listener filter
<config_udp_listener_filters_udp_proxy>`.

.. _arch_overview_dns_filter:

DNS filter
----------

Envoy supports responding to DNS requests by configuring a :ref:`UDP listener DNS Filter
<config_udp_listener_filters_dns_filter>`.

The DNS filter supports responding to forward queries for ``A`` and ``AAAA`` records.

The answers are discovered from statically configured resources, clusters, or external DNS servers.

The filter will return DNS responses up to to 512 bytes. If domains are configured with multiple addresses,
or clusters with multiple endpoints, Envoy will return each discovered address up to the
aforementioned size limit.

.. _arch_overview_connection_limit:

Connection limiting filter
--------------------------

Envoy supports local (non-distributed) connection limiting of L4 connections via the
:ref:`Connection limit filter <config_network_filters_connection_limit>` and runtime
connection limiting via the :ref:`Runtime listener connection limit <config_listeners_runtime>`.
