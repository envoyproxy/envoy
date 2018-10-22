.. _arch_overview_data_sharing_between_filters:

Envoy provides the following mechanisms for the transfer of configuration,
metadata and per-request/connection state to, from and between filters, as
well as to other core subsystems  (e.g., access logging).


Static State
============

Static state is any immutable state specified at configuration load time
(e.g., through XDS). There are three categories of static state:

Metadata
--------

Several parts of Envoy configuration (e.g. listeners, routes, clusters )
contain a metadata field (map<string, Protobuf::Struct>) where arbitrary
key-value pairs can be encoded.  The typical pattern is to use the filter
names in reverse DNS format as the key and encode filter specific
configuration metadata in the value. This metadata is immutable and shared
across all requests/connections. Such config metadata is usually provided
during bootstrap time or as part of XDS. For example, weighted clusters in
HTTP routes use the metadata to indicate the labels on the endpoints
corresponding to the weighted cluster. Another example, the subset load
balancer uses the metadata from the route entry corresponding to the
weighted cluster to select appropriate endpoints in a cluster

Typed Metadata
--------------

Metadata as such is untyped. It incurs a performance const when the caller
wants to convert it to some typed class object and act on it. Typed
Metadata solves this problem by allowing Filters to register a one time
conversion logic for a specific key. Incoming config metadata (via XDS) is
converted to class objects at config load time. Filters can then obtain a
typed variant of the metadata (usually protobuf) at runtime (per
request/connection), thereby eliminating the need for filters to repeatedly
convert from Protobuf::Struct to some internal object during
request/connection processing.

For example, if a filter that desires to have a convenience wrapper class
over the opaque metadata with key "xxx.service.policy" in ClusterInfo, will
register a ServicePolicyFactory that inherits from
ClusterTypedMetadataFactory. The factory translates the protobuf.Struct
into an instance of ServicePolicy class (inherited from
FilterState::Object). When a Cluster is created, the associated
ServicePolicy instance will be created and cached.

Router:perFilterConfig
----------------------

In HTTP routes, per_filter_config is a special case of typed metadata where
filters can  have route/virtualhost specific configuration in addition to a
global filter config common to all routes. This configuration is converted
and embedded into the route table. Its upto the filter implementation to
treat the route-specific filter config as a replacement to global config or
an enhancement. The fault filter uses this technique to provide per-route
fault configuration.

PerFilterConfig is a map<string, Struct>. The Connection manager iterates
over this map and invokes the filter factory interface
createRouteSpecificFilterConfig to parse/validate the struct value and
convert it into a typed class object that’s stored with the route
itself. Filters can then query the route-specific filter config during
request processing.

perFilterConfig shares same traits and design goals as the typed
metadata. Its a specialized implementation focussing on per-route filter
configuration (whether the filter implementation treats it as metadata or
actual configuration is upto the filter author).


Dynamic State
=============

Dynamic state is generated per network connection or per HTTP
stream. Dynamic state can be mutable if desired by the filter generating
the state.

Envoy::Network::Connection and Envoy::Http::Filter provide a StreamInfo
object that contains information about the current TCP connection and HTTP
stream (i.e., HTTP request/response pair) respectively. StreamInfo contains
a set of fixed attributes as part of the class definition (e.g., HTTP
protocol, requested server name, etc.). In addition, it provides a facility
to store typed/untyped objects (map<string,
FilterState::Object>). FilterState objects can be either write-once, or
write-many.

*Note*: Existing dynamic metadata interface will be removed. Instead,
filter authors can store the same protobuf structs under the read-write
filterstate.

