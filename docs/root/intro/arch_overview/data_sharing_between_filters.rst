.. _arch_overview_data_sharing_between_filters:

Envoy provides the following mechanisms for the transfer of configuration,
metadata and per-request/connection state to, from and between filters, as
well as to other core subsystems (e.g., access logging).


Static State
============

Static state is any immutable state specified at configuration load time
(e.g., through xDS). There are three categories of static state:

Metadata
--------

Several parts of Envoy configuration (e.g. listeners, routes, clusters)
contain a :ref:`metadata <envoy_api_msg_core.Metadata>` where arbitrary
key-value pairs can be encoded. The typical pattern is to use the filter
names in reverse DNS format as the key and encode filter specific
configuration metadata in the value. This metadata is immutable and shared
across all requests/connections. Such config metadata is usually provided
during bootstrap time or as part of xDS. For example, weighted clusters in
HTTP routes use the metadata to indicate the labels on the endpoints
corresponding to the weighted cluster. Another example, the subset load
balancer uses the metadata from the route entry corresponding to the
weighted cluster to select appropriate endpoints in a cluster

Typed Metadata
--------------

:ref:`Metadata <envoy_api_msg_core.Metadata>` as such is untyped. Before
acting on the metadata, callers typically convert it to a typed class
object. The cost of conversion becomes non-negligible when performed
repeatedly (e.g., for each request stream or connection). Typed Metadata
solves this problem by allowing filters to register a one time conversion
logic for a specific key. Incoming config metadata (via xDS) is converted
to class objects at config load time. Filters can then obtain a typed
variant of the metadata at runtime (per request or connection), thereby
eliminating the need for filters to repeatedly convert from
`ProtobufWkt::Struct` to some internal object during request/connection
processing.

For example, a filter that desires to have a convenience wrapper class over
an opaque metadata with key `xxx.service.policy` in `ClusterInfo` could
register a factory `ServicePolicyFactory` that inherits from
`ClusterTypedMetadataFactory`. The factory translates the `ProtobufWkt::Struct`
into an instance of `ServicePolicy` class (inherited from
`FilterState::Object`). When a `Cluster` is created, the associated
`ServicePolicy` instance will be created and cached. Note that typed
metadata is not a new source of metadata. It is obtained from metadata that
is specified as part of the configuration.


HTTP Per-Route Filter Configuration
-----------------------------------

In HTTP routes, :ref:`per_filter_config
<envoy_api_field_route.VirtualHost.per_filter_config>` allows HTTP filters
to have virtualhost/route-specific configuration in addition to a global
filter config common to all virtual hosts. This configuration is converted
and embedded into the route table. It is up to the HTTP filter
implementation to treat the route-specific filter config as a replacement
to global config or an enhancement. For example, the HTTP fault filter uses
this technique to provide per-route fault configuration.

`per_filter_config` is a `map<string, ProtobufWkt::Struct>`. The Connection
manager iterates over this map and invokes the filter factory interface
`createRouteSpecificFilterConfig` to parse/validate the struct value and
convert it into a typed class object that’s stored with the route
itself. HTTP filters can then query the route-specific filter config during
request processing.


Dynamic State
=============

Dynamic state is generated per network connection or per HTTP
stream. Dynamic state can be mutable if desired by the filter generating
the state.

`Envoy::Network::Connection` and `Envoy::Http::Filter` provide a
`StreamInfo` object that contains information about the current TCP
connection and HTTP stream (i.e., HTTP request/response pair)
respectively. `StreamInfo` contains a set of fixed attributes as part of
the class definition (e.g., HTTP protocol, requested server name, etc.). In
addition, it provides a facility to store typed objects in a map
(`map<string, FilterState::Object>`). The state stored per filter can be
either write-once (immutable), or write-many (mutable).
