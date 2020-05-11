What is Envoy
--------------

Envoy is an L7 proxy and communication bus designed for large modern service oriented architectures.
The project was born out of the belief that:

  *The network should be transparent to applications. When network and application problems do occur
  it should be easy to determine the source of the problem.*

In practice, achieving the previously stated goal is incredibly difficult. Envoy attempts to do so
by providing the following high level features:

**Out of process architecture:** Envoy is a self contained process that is designed to run
alongside every application server. All of the Envoys form a transparent communication mesh in which
each application sends and receives messages to and from localhost and is unaware of the network
topology. The out of process architecture has two substantial benefits over the traditional library
approach to service to service communication:

* Envoy works with any application language. A single Envoy deployment can form a mesh between
  Java, C++, Go, PHP, Python, etc. It is becoming increasingly common for service oriented
  architectures to use multiple application frameworks and languages. Envoy transparently bridges
  the gap.
* As anyone that has worked with a large service oriented architecture knows, deploying library
  upgrades can be incredibly painful. Envoy can be deployed and upgraded quickly across an
  entire infrastructure transparently.

**Modern C++11 code base:** Envoy is written in C++11. Native code was chosen because we
believe that an architectural component such as Envoy should get out of the way as much as possible.
Modern application developers already deal with tail latencies that are difficult to reason about
due to deployments in shared cloud environments and the use of very productive but not particularly
well performing languages such as PHP, Python, Ruby, Scala, etc. Native code provides generally
excellent latency properties that don't add additional confusion to an already confusing situation.
Unlike other native code proxy solutions written in C, C++11 provides both excellent developer
productivity and performance.

**L3/L4 filter architecture:** At its core, Envoy is an L3/L4 network proxy. A pluggable
:ref:`filter <arch_overview_network_filters>` chain mechanism allows filters to be written to
perform different TCP proxy tasks and inserted into the main server. Filters have already been
written to support various tasks such as raw :ref:`TCP proxy <arch_overview_tcp_proxy>`,
:ref:`HTTP proxy <arch_overview_http_conn_man>`, :ref:`TLS client certificate
authentication <arch_overview_ssl_auth_filter>`, etc.

**HTTP L7 filter architecture:** HTTP is such a critical component of modern application
architectures that Envoy :ref:`supports <arch_overview_http_filters>` an additional HTTP L7 filter
layer. HTTP filters can be plugged into the HTTP connection management subsystem that perform
different tasks such as :ref:`buffering <config_http_filters_buffer>`, :ref:`rate limiting
<arch_overview_global_rate_limit>`, :ref:`routing/forwarding <arch_overview_http_routing>`, sniffing
Amazon's :ref:`DynamoDB <arch_overview_dynamo>`, etc.

**First class HTTP/2 support:** When operating in HTTP mode, Envoy :ref:`supports
<arch_overview_http_protocols>` both HTTP/1.1 and HTTP/2. Envoy can operate as a transparent
HTTP/1.1 to HTTP/2 proxy in both directions. This means that any combination of HTTP/1.1 and HTTP/2
clients and target servers can be bridged. The recommended service to service configuration uses
HTTP/2 between all Envoys to create a mesh of persistent connections that requests and responses can
be multiplexed over. Envoy does not support SPDY as the protocol is being phased out.

**HTTP L7 routing:** When operating in HTTP mode, Envoy supports a
:ref:`routing <arch_overview_http_routing>` subsystem that is capable of routing and redirecting
requests based on path, authority, content type, :ref:`runtime <arch_overview_runtime>` values, etc.
This functionality is most useful when using Envoy as a front/edge proxy but is also leveraged when
building a service to service mesh.

**gRPC support:** `gRPC <https://www.grpc.io/>`_ is an RPC framework from Google that uses HTTP/2
as the underlying multiplexed transport. Envoy :ref:`supports <arch_overview_grpc>` all of the
HTTP/2 features required to be used as the routing and load balancing substrate for gRPC requests
and responses. The two systems are very complementary.

**MongoDB L7 support:** `MongoDB <https://www.mongodb.com/>`_ is a popular database used in modern
web applications. Envoy :ref:`supports <arch_overview_mongo>` L7 sniffing, statistics production,
and logging for MongoDB connections.

**DynamoDB L7 support**: `DynamoDB <https://aws.amazon.com/dynamodb/>`_ is Amazon’s hosted key/value
NOSQL datastore. Envoy :ref:`supports <arch_overview_dynamo>` L7 sniffing and statistics production
for DynamoDB connections.

**Service discovery and dynamic configuration:** Envoy optionally consumes a layered set of
:ref:`dynamic configuration APIs <arch_overview_dynamic_config>` for centralized management.
The layers provide an Envoy with dynamic updates about: hosts within a backend cluster, the
backend clusters themselves, HTTP routing, listening sockets, and cryptographic material.
For a simpler deployment, backend host discovery can be
:ref:`done through DNS resolution <arch_overview_service_discovery_types_strict_dns>`
(or even
:ref:`skipped entirely <arch_overview_service_discovery_types_static>`),
with the further layers replaced by static config files.

**Health checking:** The :ref:`recommended <arch_overview_service_discovery_eventually_consistent>`
way of building an Envoy mesh is to treat service discovery as an eventually consistent process.
Envoy includes a :ref:`health checking <arch_overview_health_checking>` subsystem which can
optionally perform active health checking of upstream service clusters. Envoy then uses the union of
service discovery and health checking information to determine healthy load balancing targets. Envoy
also supports passive health checking via an :ref:`outlier detection
<arch_overview_outlier_detection>` subsystem.

**Advanced load balancing:** :ref:`Load balancing <arch_overview_load_balancing>` among different
components in a distributed system is a complex problem. Because Envoy is a self contained proxy
instead of a library, it is able to implement advanced load balancing techniques in a single place
and have them be accessible to any application. Currently Envoy includes support for :ref:`automatic
retries <arch_overview_http_routing_retry>`, :ref:`circuit breaking <arch_overview_circuit_break>`,
:ref:`global rate limiting <arch_overview_global_rate_limit>` via an external rate limiting service,
:ref:`request shadowing <envoy_v3_api_msg_config.route.v3.RouteAction.RequestMirrorPolicy>`, and
:ref:`outlier detection <arch_overview_outlier_detection>`. Future support is planned for request
racing.

**Front/edge proxy support:** Although Envoy is primarily designed as a service to service
communication system, there is benefit in using the same software at the edge (observability,
management, identical service discovery and load balancing algorithms, etc.). Envoy includes enough
features to make it usable as an edge proxy for most modern web application use cases. This includes
:ref:`TLS <arch_overview_ssl>` termination, HTTP/1.1 and HTTP/2 :ref:`support
<arch_overview_http_protocols>`, as well as HTTP L7 :ref:`routing <arch_overview_http_routing>`.

**Best in class observability:** As stated above, the primary goal of Envoy is to make the network
transparent. However, problems occur both at the network level and at the application level. Envoy
includes robust :ref:`statistics <arch_overview_statistics>` support for all subsystems. `statsd
<https://github.com/etsy/statsd>`_ (and compatible providers) is the currently supported statistics
sink, though plugging in a different one would not be difficult. Statistics are also viewable via
the :ref:`administration <operations_admin_interface>` port. Envoy also supports distributed
:ref:`tracing <arch_overview_tracing>` via thirdparty providers.

Design goals
^^^^^^^^^^^^

A short note on the design goals of the code itself: Although Envoy is by no means slow (we have
spent considerable time optimizing certain fast paths), the code has been written to be modular and
easy to test versus aiming for the greatest possible absolute performance. It's our view that this
is a more efficient use of time given that typical deployments will be alongside languages and
runtimes many times slower and with many times greater memory usage.
