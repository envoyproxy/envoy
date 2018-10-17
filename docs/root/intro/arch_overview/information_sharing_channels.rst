.. _arch_overview_information_sharing_channels:

Envoy provides the following mechanisms for filters to communicate data to
other filters, or to other core subsystems in Envoy (e.g., access logging).

Config-related State
====================

Metadata (immutable)
--------------------

Several parts of Envoy configuration (e.g. routes) contain a metadata field
where abitrary string-key-to-protobufStruct value pairs can be
encoded. These metadata provide additional context about the configuration
element. This metadata is immutable and shared across all
requests/connections. Such config metadata is usually provided during
bootstrap time or as part of XDS. For example, weighted clusters in HTTP
routes use the metadata to indicate the labels on the endpoints
corresponding to the weighted cluster. The subset load balancer uses the
metadata from the route entry corresponding to the weighted cluster to
select appropriate endpoints in a cluster.

Typed Metadata (immutable) [Not in Envoy yet]
---------------------------------------------

Typed metadata is similar to Config Metadata in principle. The key
difference is that Typed Metadata allows the caller to obtain a typed
object instead of an opaque protobuf Struct. The conversion from
Protobuf::Struct to the typed object happens during config load time
thereby eliminating the need for filters to repeatedly convert from
Protobuf::Struct to some internal object during request/connection
processing. Filters can register their translation logic to convert a
metadata entry into a class object that has access functions and other
utility functions attached to the class.

For example, if a filter that desires to have a convenience wrapper class
over the opaque metadata with key "xxx.service.policy" in ClusterInfo, will
register a ServicePolicyFactory that inherits from
ClusterTypedMetadataFactory. The factory translates the protobuf.Struct
into an instance of ServicePolicy class (inherited from
FilterState::Object). When a Cluster is created, the associated
ServicePolicy instance will be created and cached.

Router:perFilterConfig
----------------------

In HTTP routes, per_filter_config is another generic
string-key-to-protobuf-Struct map that allows filters to have
route/virtualhost specific configuration in *addition* to a global filter
config common to all routes. Its upto the filter implementation to treat
the route-specific filter config as a replacement to global config or an
enhancement.

The Connection manager iterates over this map and invokes the filter
factory interface createRouteSpecificFilterConfig to parse/validate the
struct value and convert it into a typed class object thats stored with the
route itself. Filters can then query the route-specific filter config
during request processing.

perFilterConfig shares same traits and design goals as the typed metadata.
Its a specialized implementation focussing on per-route filter
configuration (whether the filter implementation treats it as metadata or
actual configuration is upto the filter author).


Per-request/per-connection State
================================

Envoy::Network::Connection and Envoy::Http::Filter provide a StreamInfo
object that contains information about the current TCP connection and HTTP
stream respectively. StreamInfo contains a set of fixed attributes as part
of the class definition (e.g., HTTP protocol, requested server name,
etc.). In addition, it provides abstractions over the following types of
state:

StreamInfo - DynamicMetadata (mutable)
------------------------------------

A generic map of string keys and Protobuf structs. Filters can store state
into DynamicMetadata with pre-defined keys, which can then be accessed by
other filters. Multiple writes to the same key will be merged. For example,
the HeaderToMetadataFilter generates metadata from HTTP headers and stores
it with under the key "envoy.lb", which is then consumed by the Router
filter to make a load balancing decision based on metadata match result.

StreamInfo - PerRequestState/PerConnectionState (renamed to dynamicAttributes) [write once]
-------------------------------------------------------------------------------------------

DynamicAttributes are similar in structure to StreamInfo.dynamicMetadata,
except they allow filters to store arbitrarily typed objects instead of
protobuf structs. Attributes can either be singleton or list from with
append semantics. For example, Router::HeaderFormatter uses perRequestState
to retrieve named (user specified) per-request attribute for logging purposes.
Multiple writes to the same key for singleton objects will throw an exception.

Well-known dynamic attributes:
------------------------------

Consider the following filter stack, along with the repo/authorship domain
of these filter:

    filter1 (envoy) -->add envoy.conn.attributes.addToList("foo"="bar")
       |
       |
    filter2 (company-internal codec)->add envoy.conn.attributes.addToList("scooby"="doo")
       |
       |
    filter3 (another OSS project. say Istio mixer)
       |
       |---->telemetry -->send all envoy.conn.attributes to telemetry server
       |
    filter4 (company-internal policy) --> if envoy.conn.attributes[foo]==bar send 404

The composition of the filter chain is not known in advance to each filter.
The filters are not authored at the same time either. Rather than encoding
knowledge of who will access a filter's dynamic attributes apriori, storing
these attributes under a well-known key (envoy.conn.attributes) allows
multiple filters to evolve independent of one another while still
consuming/acting on the data generated by others. Since StreamInfo manages
the lifecycle of these attribute objects, the dynamic attributes are
destroyed at the end of a request or a connection.

We need only two/three well known attributes:
1. connection level attributes - generated by network filters
2. HTTP connection manager level attributes - generated by filters in http
   filter stack
3. Thrift?

