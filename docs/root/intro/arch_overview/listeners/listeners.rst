.. _arch_overview_listeners:

Listeners
=========

The Envoy configuration supports any number of listeners within a single process. Generally we
recommend running a single Envoy per machine regardless of the number of configured listeners. This
allows for easier operation and a single source of statistics.

Envoy supports both :ref:`TCP <arch_overview_listeners_tcp>` and :ref:`UDP <arch_overview_listeners_udp>` listeners.

.. _arch_overview_listeners_tcp:

TCP
---

Each listener is independently configured with :ref:`filter_chains
<envoy_v3_api_field_config.listener.v3.Listener.filter_chains>`, where an individual
:ref:`filter_chain <envoy_v3_api_msg_config.listener.v3.FilterChain>` is selected based on its
:ref:`filter_chain_match <envoy_v3_api_msg_config.listener.v3.FilterChainMatch>` criteria.

An individual :ref:`filter_chain <envoy_v3_api_msg_config.listener.v3.FilterChain>` is
composed of one or more network level (L3/L4) :ref:`filters <arch_overview_network_filters>`.

When a new connection is received on a listener, the appropriate
:ref:`filter_chain <envoy_v3_api_msg_config.listener.v3.FilterChain>` is selected, and the
configured connection-local filter stack is instantiated and begins processing subsequent events.

The generic listener architecture is used to perform the vast majority of different proxy tasks that
Envoy is used for (e.g., :ref:`rate limiting <arch_overview_global_rate_limit>`, :ref:`TLS client
authentication <arch_overview_ssl_auth_filter>`, :ref:`HTTP connection management
<arch_overview_http_conn_man>`, MongoDB :ref:`sniffing <arch_overview_mongo>`, raw :ref:`TCP proxy
<arch_overview_tcp_proxy>`, etc.).

Listeners are optionally also configured with some number of :ref:`listener filters
<arch_overview_listener_filters>`. These filters are processed before the network level filters,
and have the opportunity to manipulate the connection metadata, usually to influence how the
connection is processed by later filters or clusters.

Listeners can also be fetched dynamically via the :ref:`listener discovery service (LDS)
<config_listeners_lds>`.

.. tip::
   See the Listener :ref:`configuration <config_listeners>`,
   :ref:`protobuf <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>` and
   :ref:`components <envoy_v3_api_file_envoy/config/listener/v3/listener_components.proto>`
   sections for reference documentation.

.. _arch_overview_listeners_udp:

UDP
---

Envoy also supports UDP listeners and specifically :ref:`UDP listener filters
<config_udp_listener_filters>`.

UDP listener filters are instantiated once per worker and are global to that worker.

Each listener filter processes each UDP datagram that is received by the worker
listening on the port.

In practice, UDP listeners are configured with the ``SO_REUSEPORT`` kernel option which
will cause the kernel to consistently hash each UDP 4-tuple to the same worker. This allows a
UDP listener filter to be "session" oriented if it so desires. A built-in example of this
functionality is the :ref:`UDP proxy <config_udp_listener_filters_udp_proxy>` listener filter.
