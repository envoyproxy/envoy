# Hazelcast Http Cache Plugin
Hazelcast Http Cache provides a pluggable storage implementation backed by Hazelcast In Memory Data Grid for the http
cache filter. Using Hazelcast C++ client, the plugin does not store any http response locally but in a distributed map
provided by Hazelcast cluster. To enable the cache plugin, the network configuration belongs to a cluster to be
connected must be set on the cache plugin configuration.

## Offered cache modes
The plugin comes with two modes:

 - **Unified**
A cached http response is stored as a single entry in the cache. On a range http request, regardless of the requested
range, the whole response body is fetched from the cache and then only the desired bytes are served along with the
headers and trailers (if any). This mode is handy where response body sizes are reasonably large (up to 10 KB), or
range requests are not frequent, or they are not allowed at all.

 - **Divided**
A cached http response is stored as multiple entries in the cache. Two separate maps are used to store a single
response. In one of them, response headers, body size, and trailers (if any) are stored. In the other one, the
corresponding response body is stored in multiple entries each of which has a certain size configured via `partition
size` in the plugin configuration. That is, for a response of size 50 KB, if the configured partition size is 20 KB,
then three different entries will be created to store the body of this response:
    - Body<1> : 0 - 19 KB
    - Body<2> : 20 - 39 KB
    - Body<3> : 40 - 50 KB

    On a range request, not the whole body for a response but only the necessary partitions are fetched from the
    cache. This option helps to serve range requests faster and in a stream-like fashion but comes with a cost. Every
    body entry has its own fixed memory cost and hence partitioned entries need larger memory than the actual body
    size. Also, to keep these partitioned cache entries even, extra operations - not necessarily asynchronous, might
    be needed (i.e. cleaning up a malformed body sequence, recovery from a mismatch between body and header, etc.).

## Connecting to a Hazelcast cluster
**NOTE:** The plugin uses the client with version 3.12.1 and hence it is not yet compatible with Hazelcast 4.x.
Hazelcast version 3.12.x is recommended for the server-side.

Before starting the cache plugin, there must be a running Hazelcast cluster. Hazelcast instances might be started
as a sidecar to Envoy, form up a cluster using Hazelcast Kubernetes plugin, etc. The only information the plugin needs
will be the addresses and ports of the cluster members and the group name of the cluster. Providing the address of only
one member in the cluster will be enough for the connection but using more than one is recommended.

Related links: [Hazelcast Docker](https://hub.docker.com/r/hazelcast/hazelcast/), [Hazelcast Kubernetes Plugin]
(https://github.com/hazelcast/hazelcast-kubernetes)

## Configuring Hazelcast cluster for the cache
Eviction, maximum size, and other related properties for the cache must be configured on the server-side. That is,
in `hazelcast.xml` before starting the cluster:
```xml
<!-- use wildcard for the map name to configure the cache -->
<map name="<app_prefix>*">

    <!-- Customizable cache configurations -->
    <max-size policy="PER_NODE">1000</max-size>
    <eviction-policy>LFU</eviction-policy>
    <eviction-percentage>25</eviction-percentage>
    <time-to-live-seconds>180</time-to-live-seconds>

    <!--

    NOTE: The plugin is not optimized for max-idle-seconds based
    eviction hence prefer to use TTL.

    for more configuration options, see Hazelcast doc:

    https://docs.hazelcast.org/docs/3.12.6/manual/html-single/index.html#map

    OBJECT in-memory format will not have any advantage but extra serialization cost here. Setting it to
    BINARY is the best fit for
    the plugin.

    -->
    <in-memory-format>BINARY</in-memory-format>
    <statistics-enabled>true</statistics-enabled>
</map>
```
When one of the clients connected to the cluster loses its connection, if the client has acquired the lock for a
key, this will cause this key to be unusable. To prevent such a scenario in a possible connection failure, the
maximum time limit for the locks should be set on the server-side (not necessarily to be 60 seconds):
```xml
<properties>
    ...
    <property name="hazelcast.lock.max.lease.time.seconds">60</property>
    ...
</properties>
```
**NOTE**: Setting this property will affect not only the http cache but all other data structures in the cluster.

## Statistics
Cache statistics are not collected locally. Instead, cluster-wide statistics should be observed on Hazelcast
Management Center. When the cache plugin starts, one of the very first logs will be saying the map name used for
the cache. The statistics can be observed with that name under the `maps` section on the management center.

## Using a single cache for multiple filters
Each distributed map in a Hazelcast cluster is differentiated by its name for the same key and value types. Thus,
all the plugins connected to the same cluster will use the same map for responses only if they have the same cache
mode and the app prefix (and the same partition size for divided mode) in the plugin configuration. The filters
configured with the same partition size and cache mode but different prefixes will create two different http caches.