.. _arch_overview_service_discovery:

Service discovery
=================

When an upstream cluster is defined in the :ref:`configuration <envoy_v3_api_msg_config.cluster.v3.Cluster>`,
Envoy needs to know how to resolve the members of the cluster. This is known as *service discovery*.

.. _arch_overview_service_discovery_types:

Supported service discovery types
---------------------------------

.. _arch_overview_service_discovery_types_static:
.. _extension_envoy.clusters.static:

Static
^^^^^^

Static is the simplest service discovery type. The configuration explicitly specifies the resolved
network name (IP address/port, unix domain socket, etc.) of each upstream host.

.. _arch_overview_service_discovery_types_strict_dns:
.. _extension_envoy.clusters.strict_dns:

Strict DNS
^^^^^^^^^^

When using strict DNS service discovery, Envoy will continuously and asynchronously resolve the
specified DNS targets. Each returned IP address in the DNS result will be considered an explicit
host in the upstream cluster. This means that if the query returns three IP addresses, Envoy will
assume the cluster has three hosts, and all three should be load balanced to. If a host is removed
from the result Envoy assumes it no longer exists and will drain traffic from any existing
connection pools. Consequently, if a successful DNS resolution returns 0 hosts, Envoy will assume
that the cluster does not have any hosts. Note that Envoy never synchronously resolves DNS in the
forwarding path. At the expense of eventual consistency, there is never a worry of blocking on a
long running DNS query.

If a single DNS name resolves to the same IP multiple times, these IPs will be de-duplicated.

If multiple DNS names resolve to the same IP, health checking will *not* be shared.
This means that care should be taken if active health checking is used with DNS names that resolve
to the same IPs: if an IP is repeated many times between DNS names it might cause undue load on the
upstream host.

If :ref:`respect_dns_ttl <envoy_v3_api_field_config.cluster.v3.Cluster.respect_dns_ttl>` is enabled, DNS record TTLs and
:ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` are used to control DNS refresh rate.
For strict DNS cluster, if the minimum of all record TTLs is 0, :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>`
will be used as the cluster's DNS refresh rate. :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>`
defaults to 5000ms if not specified. The :ref:`dns_failure_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_failure_refresh_rate>`
controls the refresh frequency during failures, and, if not configured, the DNS refresh rate will be used.

DNS resolving emits :ref:`cluster statistics <config_cluster_manager_cluster_stats>` fields *update_attempt*, *update_success* and *update_failure*.

.. _arch_overview_service_discovery_types_logical_dns:
.. _extension_envoy.clusters.logical_dns:

Logical DNS
^^^^^^^^^^^

Logical DNS uses a similar asynchronous resolution mechanism to strict DNS. However, instead of
strictly taking the results of the DNS query and assuming that they comprise the entire upstream
cluster, a logical DNS cluster only uses the first IP address returned *when a new connection needs
to be initiated*. Thus, a single logical connection pool may contain physical connections to a
variety of different upstream hosts. Connections are never drained,
including on a successful DNS resolution that returns 0 hosts.

This service discovery type is
optimal for large scale web services that must be accessed via DNS. Such services typically use
round robin DNS to return many different IP addresses. Typically a different result is returned for
each query. If strict DNS were used in this scenario, Envoy would assume that the cluster’s members
were changing during every resolution interval which would lead to draining connection pools,
connection cycling, etc. Instead, with logical DNS, connections stay alive until they get cycled.
When interacting with large scale web services, this is the best of all possible worlds:
asynchronous/eventually consistent DNS resolution, long lived connections, and zero blocking in the
forwarding path.

If :ref:`respect_dns_ttl <envoy_v3_api_field_config.cluster.v3.Cluster.respect_dns_ttl>` is enabled, DNS record TTLs and
:ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>` are used to control DNS refresh rate.
For logical DNS cluster, if the TTL of first record is 0, :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>`
will be used as the cluster's DNS refresh rate. :ref:`dns_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_refresh_rate>`
defaults to 5000ms if not specified. The :ref:`dns_failure_refresh_rate <envoy_v3_api_field_config.cluster.v3.Cluster.dns_failure_refresh_rate>`
controls the refresh frequency during failures, and, if not configured, the DNS refresh rate will be used.

DNS resolving emits :ref:`cluster statistics <config_cluster_manager_cluster_stats>` fields *update_attempt*, *update_success* and *update_failure*.

.. _arch_overview_service_discovery_types_original_destination:

Original destination
^^^^^^^^^^^^^^^^^^^^

Original destination cluster can be used when incoming connections are redirected to Envoy either
via an iptables REDIRECT or TPROXY target or with Proxy Protocol. In these cases requests routed
to an original destination cluster are forwarded to upstream hosts as addressed by the redirection
metadata, without any explicit host configuration or upstream host discovery.
Connections to upstream hosts are pooled and unused hosts are flushed out when they have been not use by any connection
pool longer than :ref:`cleanup_interval <envoy_v3_api_field_config.cluster.v3.Cluster.cleanup_interval>`, which defaults
to 5000ms. If the original destination address is not available, no upstream connection is opened.  Envoy can also
pickup the original destination from a :ref:`HTTP header
<arch_overview_load_balancing_types_original_destination_request_header>`. Original destination service discovery must
be used with the original destination :ref:`load balancer <arch_overview_load_balancing_types_original_destination>`.
When using the original destination cluster for HTTP upstreams, please set :ref:`idle_timeout
<faq_configuration_connection_timeouts>` to 5min to limit the duration of the upstream HTTP connections.

.. _arch_overview_service_discovery_types_eds:
.. _extension_envoy.clusters.eds:

Endpoint discovery service (EDS)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The *endpoint discovery service* is a :ref:`xDS management server based on gRPC or REST-JSON API server
<config_overview_management_server>` used by Envoy to fetch cluster members. The cluster members are called
"endpoint" in Envoy terminology. For each cluster, Envoy fetch the endpoints from the discovery service. EDS is the
preferred service discovery mechanism for a few reasons:

* Envoy has explicit knowledge of each upstream host (vs. routing through a DNS resolved load
  balancer) and can make more intelligent load balancing decisions.
* Extra attributes carried in the discovery API response for each host inform Envoy of the host’s
  load balancing weight, canary status, zone, etc. These additional attributes are used globally
  by the Envoy mesh during load balancing, statistic gathering, etc.

The Envoy project provides reference gRPC implementations of EDS and
:ref:`other discovery services <arch_overview_dynamic_config>`
in both `Java <https://github.com/envoyproxy/java-control-plane>`_
and `Go <https://github.com/envoyproxy/go-control-plane>`_.

.. _arch_overview_service_discovery_types_custom:

Custom cluster
^^^^^^^^^^^^^^

Envoy also supports custom cluster discovery mechanism. Custom clusters are specified using
:ref:`cluster_type field <envoy_v3_api_field_config.cluster.v3.Cluster.cluster_type>` on the cluster configuration.

Generally active health checking is used in conjunction with the eventually consistent service
discovery service data to making load balancing and routing decisions. This is discussed further in
the following section.

.. _arch_overview_service_discovery_eventually_consistent:

On eventually consistent service discovery
------------------------------------------

Many existing RPC systems treat service discovery as a fully consistent process. To this end, they
use fully consistent leader election backing stores such as Zookeeper, etcd, Consul, etc. Our
experience has been that operating these backing stores at scale is painful.

Envoy was designed from the beginning with the idea that service discovery does not require full
consistency. Instead, Envoy assumes that hosts come and go from the mesh in an eventually consistent
way. Our recommended way of deploying a service to service Envoy mesh configuration uses eventually
consistent service discovery along with :ref:`active health checking <arch_overview_health_checking>`
(Envoy explicitly health checking upstream cluster members) to determine cluster health. This
paradigm has a number of benefits:

* All health decisions are fully distributed. Thus, network partitions are gracefully handled
  (whether the application gracefully handles the partition is a different story).
* When health checking is configured for an upstream cluster, Envoy uses a 2x2 matrix to determine
  whether to route to a host:

.. csv-table::
  :header: Discovery Status, Health Check OK, Health Check Failed
  :widths: 1, 1, 2

  Discovered, Route, Don't Route
  Absent, Route, Don't Route / Delete

Host discovered / health check OK
  Envoy **will route** to the target host.

Host absent / health check OK:
  Envoy **will route** to the target host. This is very important since the design assumes that the
  discovery service can fail at any time. If a host continues to pass health check even after becoming
  absent from the discovery data, Envoy will still route. Although it would be impossible to add new
  hosts in this scenario, existing hosts will continue to operate normally. When the discovery service
  is operating normally again the data will eventually re-converge.

Host discovered / health check FAIL
  Envoy **will not route** to the target host. Health check data is assumed to be more accurate than
  discovery data.

Host absent / health check FAIL
  Envoy **will not route and will delete** the target host. This
  is the only state in which Envoy will purge host data.
