Envoy Connection Limit Filter

# Background
Network connections are a limited resource that we need some functionality to protect. Envoy has the capability to limit the rate of new connections via the L4 [local rate limit filter](https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/local_rate_limit_filter). It would be useful to be able to limit the total number of connections, specifically on a per filter chain basis or based on some descriptors from the request.
# Goals
1. Protection for resources such as connections, CPU, memory, etc. by limiting the total number of active connections on a per filter chain basis or based on some descriptors from the request.
1. Preventing any particular client from consuming a large number of connections to ensure fair share of the connections.
# Filter Overview
We are planning to add a new Connection Limit Filter in Envoy since there’s no existing total connection limit functionality in Envoy. It will be a L4 filter that is designed to be configurable and extensible. This way we can prevent any one client from taking up too much network connections resource.

- The connection limit filter will be similar to the L4 local rate limit filter, but instead of enforcing the limit on connections rate, the new filter will limit the total number of active connections.
- The filter maintains an atomic counter of active connection count. It has a max connections limit value based on the configured total number of connections. When a new connection request comes, the filter tries to increment the connection counter. The connection is allowed if the counter is less than the max connections limit, otherwise the connection gets rejected. When an active connection is closed, the filter decrements the active connection counter.
- The filter may not stop connection creation but will immediately close the connections that were accepted but were deemed as overlimit.
- **Slow rejection:** The filter can stop reading from the connection and reject it after a delay instead of rejecting it right away or letting requests go through before the rejection. This way we can prevent a malicious client from opening new connections while draining their resources.
## Algorithm
The filter will use the reference counting algorithm to keep trace of active connection count.

1. Active connection count < max connection limit:
   1. Increment count for a new allowed connection.
   1. Decrement count when a connection closes.
1. Active connection count >= max connection limit:
   1. Reject the new connection request after configured delay time.
## API Reference
[extensions.filters.network.connection\_limit.v3.ConnectionLimit proto]

{

**"stat\_prefix"**: "...",

**"max\_connections"**: "...",

**"delay"**: "{...}",

**"runtime\_enabled"**: "{...}"

}

**stat\_prefix**

([string](https://developers.google.com/protocol-buffers/docs/proto#scalar), *REQUIRED*) The prefix to use when emitting [statistics](#_Statistics).

**max\_connections**

([uint64](https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#google.protobuf.UInt64Value), *REQUIRED*) The limit supplied in max connections.

**delay**

([Duration](https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#duration)) The delay in milliseconds for the slow rejection connections. If not set, this defaults to 0ms.

**runtime\_enabled**

([config.core.v3.RuntimeFeatureFlag](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/base.proto#envoy-v3-api-msg-config-core-v3-runtimefeatureflag)) Runtime flag that controls whether the filter is enabled or not. If not specified, defaults to enabled.
## Statistics
The connection limit filter outputs statistics in the *<stat\_prefix>.connection\_limit* namespace.

|**Name**|**Type**|**Description**|
| :- | :- | :- |
|connection\_limited|Counter|Total number of connections got rejected by this connection limit filter|
|active_connections|Gauge|Number of currently active connections|


