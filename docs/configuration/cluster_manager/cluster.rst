.. _config_cluster_manager_cluster:

Cluster
=======

.. code-block:: json

  {
    "name": "...",
    "type": "...",
    "connect_timeout_ms": "...",
    "per_connection_buffer_limit_bytes": "...",
    "lb_type": "...",
    "hosts": [],
    "service_name": "...",
    "health_check": "{...}",
    "max_requests_per_connection": "...",
    "circuit_breakers": "{...}",
    "ssl_context": "{...}",
    "features": "...",
    "http2_settings": "{...}",
    "cleanup_interval_ms": "...",
    "dns_refresh_rate_ms": "...",
    "dns_lookup_family": "...",
    "dns_resolvers": [],
    "outlier_detection": "{...}"
  }

.. _config_cluster_manager_cluster_name:

name
  *(required, string)* Supplies the name of the cluster which must be unique across all clusters.
  The cluster name is used when emitting :ref:`statistics <config_cluster_manager_cluster_stats>`.
  By default, the maximum length of a cluster name is limited to 60 characters. This limit can be
  increased by setting the :option:`--max-obj-name-len` command line argument to the desired value.

.. _config_cluster_manager_type:

type
  *(required, string)* The :ref:`service discovery type <arch_overview_service_discovery_types>` to
  use for resolving the cluster. Possible options are *static*, *strict_dns*, *logical_dns*,
  :ref:`*original_dst* <arch_overview_service_discovery_types_original_destination>`, and *sds*.

connect_timeout_ms
  *(required, integer)* The timeout for new network connections to hosts in the cluster specified
  in milliseconds.

.. _config_cluster_manager_cluster_per_connection_buffer_limit_bytes:

per_connection_buffer_limit_bytes
  *(optional, integer)* Soft limit on size of the cluster's connections read and write buffers.
  If unspecified, an implementation defined default is applied (1MiB).

.. _config_cluster_manager_cluster_lb_type:

lb_type
  *(required, string)* The :ref:`load balancer type <arch_overview_load_balancing_types>` to use
  when picking a host in the cluster. Possible options are *round_robin*, *least_request*,
  *ring_hash*, *random*, and *original_dst_lb*.  Note that :ref:`*original_dst_lb*
  <arch_overview_load_balancing_types_original_destination>` must be used with clusters of type
  :ref:`*original_dst* <arch_overview_service_discovery_types_original_destination>`, and may not be
  used with any other cluster type.

hosts
  *(sometimes required, array)* If the service discovery type is *static*, *strict_dns*, or
  *logical_dns* the hosts array is required. Hosts array is not allowed with cluster type
  *original_dst*. How it is specified depends on the type of service discovery:

  static
    Static clusters must use fully resolved hosts that require no DNS lookups. Both TCP and unix
    domain sockets (UDS) addresses are supported. A TCP address looks like:

    ``tcp://<ip>:<port>``

    A UDS address looks like:

    ``unix://<file name>``

    A list of addresses can be specified as in the following example:

    .. code-block:: json

      [{"url": "tcp://10.0.0.2:1234"}, {"url": "tcp://10.0.0.3:5678"}]

  strict_dns
    Strict DNS clusters can specify any number of hostname:port combinations. All names will be
    resolved using DNS and grouped together to form the final cluster. If multiple records are
    returned for a single name, all will be used. For example:

    .. code-block:: json

      [{"url": "tcp://foo1.bar.com:1234"}, {"url": "tcp://foo2.bar.com:5678"}]

  logical_dns
    Logical DNS clusters specify hostnames much like strict DNS, however only the first host will be
    used. For example:

    .. code-block:: json

      [{"url": "tcp://foo1.bar.com:1234"}]

.. _config_cluster_manager_cluster_service_name:

service_name
  *(sometimes required, string)* This parameter is required if the service discovery type is *sds*.
  It will be passed to the :ref:`SDS API <config_cluster_manager_sds_api>` when fetching cluster
  members.

:ref:`health_check <config_cluster_manager_cluster_hc>`
  *(optional, object)* Optional :ref:`active health checking <arch_overview_health_checking>`
  configuration for the cluster. If no configuration is specified no health checking will be done
  and all cluster members will be considered healthy at all times.

max_requests_per_connection
  *(optional, integer)* Optional maximum requests for a single upstream connection. This
  parameter is respected by both the HTTP/1.1 and HTTP/2 connection pool implementations. If not
  specified, there is no limit. Setting this parameter to 1 will effectively disable keep alive.

:ref:`circuit_breakers <config_cluster_manager_cluster_circuit_breakers>`
  *(optional, object)* Optional :ref:`circuit breaking <arch_overview_circuit_break>` settings
  for the cluster.

:ref:`ssl_context <config_cluster_manager_cluster_ssl>`
  *(optional, object)* The TLS configuration for connections to the upstream cluster. If no TLS
  configuration is specified, TLS will not be used for new connections.

.. _config_cluster_manager_cluster_features:

features
  *(optional, string)* A comma delimited list of features that the upstream cluster supports.
  The currently supported features are:

  http2
    If *http2* is specified, Envoy will assume that the upstream supports HTTP/2 when making new
    HTTP connection pool connections. Currently, Envoy only supports prior knowledge for upstream
    connections. Even if TLS is used with ALPN, *http2* must be specified. As an aside this allows
    HTTP/2 connections to happen over plain text.

.. _config_cluster_manager_cluster_http2_settings:

http2_settings
  *(optional, object)* Additional HTTP/2 settings that are passed directly to the HTTP/2 codec when
  initiating HTTP connection pool connections. These are the same options supported in the HTTP connection
  manager :ref:`http2_settings <config_http_conn_man_http2_settings>` option.

.. _config_cluster_manager_cluster_cleanup_interval_ms:

cleanup_interval_ms
  *(optional, integer)* The interval for removing stale hosts from an *original_dst* cluster. Hosts
  are considered stale if they have not been used as upstream destinations during this interval.
  New hosts are added to original destination clusters on demand as new connections are redirected
  to Envoy, causing the number of hosts in the cluster to grow over time. Hosts that are not stale
  (they are actively used as destinations) are kept in the cluster, which allows connections to
  them remain open, saving the latency that would otherwise be spent on opening new connections.
  If this setting is not specified, the value defaults to 5000. For cluster types other than
  *original_dst* this setting is ignored.

.. _config_cluster_manager_cluster_dns_refresh_rate_ms:

dns_refresh_rate_ms
  *(optional, integer)* If the dns refresh rate is specified and the cluster type is either *strict_dns*,
  or *logical_dns*, this value is used as the cluster's dns refresh rate. If this setting is not specified,
  the value defaults to 5000. For cluster types other than *strict_dns* and *logical_dns* this setting is
  ignored.

.. _config_cluster_manager_cluster_dns_lookup_family:

dns_lookup_family
  *(optional, string)* The DNS IP address resolution policy. The options are *v4_only*, *v6_only*,
  and *auto*. If this setting is not specified, the value defaults to *v4_only*. When *v4_only* is selected,
  the DNS resolver will only perform a lookup for addresses in the IPv4 family. If *v6_only* is selected,
  the DNS resolver will only perform a lookup for addresses in the IPv6 family. If *auto* is specified,
  the DNS resolver will first perform a lookup for addresses in the IPv6 family and fallback to a lookup for
  addresses in the IPv4 family. For cluster types other than *strict_dns* and *logical_dns*, this setting
  is ignored.

.. _config_cluster_manager_cluster_dns_resolvers:

dns_resolvers
  *(optional, array)* If DNS resolvers are specified and the cluster type is either *strict_dns*, or
  *logical_dns*, this value is used to specify the cluster's dns resolvers. If this setting is not
  specified, the value defaults to the default resolver, which uses /etc/resolv.conf for
  configuration. For cluster types other than *strict_dns* and *logical_dns* this setting is
  ignored.

.. _config_cluster_manager_cluster_outlier_detection_summary:

:ref:`outlier_detection <config_cluster_manager_cluster_outlier_detection>`
  *(optional, object)* If specified, outlier detection will be enabled for this upstream cluster.
  See the :ref:`architecture overview <arch_overview_outlier_detection>` for more information on outlier
  detection.

.. toctree::
  :hidden:

  cluster_hc
  cluster_circuit_breakers
  cluster_ssl
  cluster_stats
  cluster_runtime
  cluster_outlier_detection
