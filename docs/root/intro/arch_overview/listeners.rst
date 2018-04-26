.. _arch_overview_listeners:

Listeners
=========

The Envoy configuration supports any number of listeners within a single process. Generally we
recommend running a single Envoy per machine regardless of the number of configured listeners. This
allows for easier operation and a single source of statistics. Currently Envoy only supports TCP
listeners.

Each listener is independently configured with some number of network level (L3/L4) :ref:`filters
<arch_overview_network_filters>`. When a new connection is received on a listener, the configured
connection local filter stack is instantiated and begins processing subsequent events. The generic
listener architecture is used to perform the vast majority of different proxy tasks that Envoy is
used for (e.g., :ref:`rate limiting <arch_overview_rate_limit>`, :ref:`TLS client authentication
<arch_overview_ssl_auth_filter>`, :ref:`HTTP connection management <arch_overview_http_conn_man>`,
MongoDB :ref:`sniffing <arch_overview_mongo>`, raw :ref:`TCP proxy <arch_overview_tcp_proxy>`,
etc.).

Listeners are optionally also configured with some number of :ref:`listener filters
<arch_overview_listener_filters>`. These filters are processed before the network level filters,
and have the opportunity to manipulate the connection metadata, usually to influence how the
connection is processed later filters or clusters.

Listeners can also be fetched dynamically via the :ref:`listener discovery service (LDS)
<config_listeners_lds>`.

Listener :ref:`configuration <config_listeners>`.
