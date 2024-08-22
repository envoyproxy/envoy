Terminology
===========

A few definitions before we dive into the main architecture documentation. Some of the definitions
are slightly contentious within the industry, however they are how Envoy uses them throughout the
documentation and codebase, so *c'est la vie*.

**Host**: An entity capable of network communication (application on a mobile phone, server, etc.).
In this documentation a host is a logical network application. A physical piece of hardware could
possibly have multiple hosts running on it as long as each of them can be independently addressed.

**Downstream**: A downstream host connects to Envoy, sends requests, and receives responses.

**Upstream**: An upstream host receives connections and requests from Envoy and returns responses.

**Listener**: A listener is a named network location (e.g., port, unix domain socket, etc.) that can
be connected to by downstream clients. Envoy exposes one or more listeners that downstream hosts
connect to.

**Cluster**: A cluster is a group of logically similar upstream hosts that Envoy connects to. Envoy
discovers the members of a cluster via :ref:`service discovery <arch_overview_service_discovery>`.
It optionally determines the health of cluster members via :ref:`active health checking
<arch_overview_health_checking>`. The cluster member that Envoy routes a request to is determined
by the :ref:`load balancing policy <arch_overview_load_balancing>`.

**Mesh**: A group of hosts that coordinate to provide a consistent network topology. In this
documentation, an “Envoy mesh” is a group of Envoy proxies that form a message passing substrate for
a distributed system comprised of many different services and application platforms.

**Runtime configuration**: Out of band realtime configuration system deployed alongside Envoy.
Configuration settings can be altered that will affect operation without needing to restart Envoy or
change the primary configuration.
