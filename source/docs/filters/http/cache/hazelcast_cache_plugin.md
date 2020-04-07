### Hazelcast Http Cache Plugin
Work in Progress--Cache filter has not implemented features. The corresponding ones are not ready for the plugin too.

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
will be the addresses and ports of the cluster members and the group information of the cluster. Providing the address
of only one member in the cluster will be enough for the connection but using more than one is recommended.

Related links: [Hazelcast Docker Hub](https://hub.docker.com/r/hazelcast/hazelcast/), [Hazelcast Kubernetes Plugin]
(https://github.com/hazelcast/hazelcast-kubernetes)

## Configuring Hazelcast cluster for the cache
Eviction, maximum size, and other related properties for the cache must be configured on the server-side
via programmatic configuration or `hazelcast.xml`.

 - **Unified Mode**

```xml
<!-- use wildcard for the map name to configure the cache since the full name is determined by the plugin -->
<map name="<app_prefix>*uni">

    <!--

    Customizable http cache configurations.
    For instance, for the configuration below:

    - 25% of the http responses will be evicted according to LRU policy when the map size hits 1000.
    - Each http responses will live at most 180 seconds in the cache.
    - If an http responses is not called for the last 90 seconds, it will be evicted immediately regardless of the TTL.

     -->
    <max-size policy="PER_NODE">1000</max-size>
    <eviction-percentage>25</eviction-percentage>
    <eviction-policy>LRU</eviction-policy>
    <time-to-live-seconds>180</time-to-live-seconds>
    <max-idle-seconds>90</max-idle-seconds>

    <!--

    For more configuration options, see Hazelcast doc:

    https://docs.hazelcast.org/docs/3.12.6/manual/html-single/index.html#map

    OBJECT in-memory format will not have any advantage but extra serialization cost here. Setting it to
    BINARY is the best fit for the plugin. The two configurations below are also the default values.

    -->
    <in-memory-format>BINARY</in-memory-format>
    <statistics-enabled>true</statistics-enabled>
</map>
```

 - **Divided Mode**

```xml
<!-- use wildcard for the map name to configure the cache since the full name is determined by the plugin.default -->
<map name="<app_prefix>*div">

    <!--

    Customizable http cache configurations for header map. The properties below will determine the
    characteristics of the http cache, not only response headers cache.

    For instance, for the configuration below:

    - 25% of the http responses will be evicted according to LRU policy when the map size hits 100.
    - Each http responses will live at most 180 seconds in the cache.

    NOTE: Although works fine, divided mode is not optimized for idle-time based eviction.

     -->
    <max-size policy="PER_NODE">100</max-size>
    <eviction-percentage>25</eviction-percentage>
    <eviction-policy>LRU</eviction-policy>
    <time-to-live-seconds>180</time-to-live-seconds>

    <!--

    For more configuration options, see Hazelcast doc:

    https://docs.hazelcast.org/docs/3.12.6/manual/html-single/index.html#map

    OBJECT in-memory format will not have any advantage but extra serialization cost here. Setting it to
    BINARY is the best fit for the plugin. The two configurations below are also the default values.

    -->
    <in-memory-format>BINARY</in-memory-format>
    <statistics-enabled>true</statistics-enabled>
</map>

<map name="<app_prefix>*body">

    <!--

    Customizable http cache configurations for body map.

    Do not set the cache configuration here. Instead, configure `<app_prefix>*div` first and set
    the properties here accordingly.

    - Do not use max-size configuration here. They will be evicted according to TTL when their header is evicted.
      Instead, the max size is (indirectly) ensured with max allowed body size configuration along with the partition
      size.
    - Use the same eviction configuration with the header map.
    - Keep TTL slightly longer than the header map (i.e. 15 seconds).

    NOTE: Although works fine, divided mode is not optimized for idle-time based eviction.

     -->
    <max-size policy="PER_NODE">0</max-size>
    <eviction-percentage>25</eviction-percentage>
    <eviction-policy>LRU</eviction-policy>
    <time-to-live-seconds>195</time-to-live-seconds>

    <!--

    OBJECT in-memory format will not have any advantage but extra serialization cost here. Setting it to
    BINARY is the best fit for the plugin. The two configurations below are also the default values.

    Statistics of this map will not have meaningful information about the cache and can be disabled if not needed.
    However, it might be useful to observe how the configured `partition_size` fits for the cached responses.

    -->
    <in-memory-format>BINARY</in-memory-format>
    <statistics-enabled>false</statistics-enabled>
</map>
```

When one of the clients connected to the cluster loses its connection, if the client has acquired the lock for a
key, this will cause this key to be unusable. To prevent such a scenario in a possible connection failure, the
maximum time limit for the locks should be set on the server-side (not necessarily to be 60 seconds). The default
value for this property is `Long.MAX`. Hence, if it is not set, on a connection failure a locked key will
be unusable permanently:
```xml
<properties>
    ...
    <!-- required for both of the cache modes -->
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