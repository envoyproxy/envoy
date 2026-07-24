.. _config_http_filters_upstream_rbac:
.. _extension_envoy.filters.http.upstream_rbac:

Upstream Role Based Access Control (RBAC) Filter
================================================

The upstream RBAC filter authorizes a request against the **selected upstream host**, before the
upstream connection is initiated. It is configured exactly like the
:ref:`RBAC filter <config_http_filters_rbac>` (it reuses the same
:ref:`RBAC <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>` configuration), but runs as an
:ref:`upstream HTTP filter <arch_overview_upstream_filters>` and evaluates its policies from the
host-selection callback rather than during request decoding.

The primary use case is protecting against SSRF by enforcing a default-deny policy on the upstream
IP address. Because evaluation happens after a host has been selected but before the connection is
established, the :ref:`upstream_ip_port
<envoy_v3_api_msg_extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher>` matcher
receives the resolved upstream address directly from host selection. By contrast, the downstream
:ref:`RBAC filter <config_http_filters_rbac>` can only match on the upstream IP when a preceding HTTP
filter (the dynamic forward proxy filter with ``save_upstream_address``) has already recorded the
selected host in filter state. Evaluating at host-selection time removes that dependency, so the
upstream IP match works for **all** cluster types — including dynamic forward proxy
``sub_cluster_config``, static, EDS, and strict DNS. When the request is denied, no upstream
connection is established and a ``403 (Forbidden)`` local reply is returned.

Since the filter is handed the downstream connection and request headers, the full set of RBAC
matchers available to the downstream :ref:`RBAC filter <config_http_filters_rbac>` (request headers,
source/destination IP and port, SSL properties, dynamic metadata, CEL attributes, ...) can be
combined with the upstream IP match.

.. attention::

  This filter must be configured as an **upstream** HTTP filter. There is no top-level
  ``upstream_http_filters`` field on a cluster; the upstream HTTP filter chain lives in one of two
  places:

  * **Per cluster** — inside the cluster's
    :ref:`typed_extension_protocol_options <envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`,
    under the ``envoy.extensions.upstreams.http.v3.HttpProtocolOptions`` entry, in its
    :ref:`http_filters <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.http_filters>`
    list (see the example below).
  * **On the router** (applies to every cluster) — via
    :ref:`Router.upstream_http_filters <envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>`.

  Either chain must end with the ``envoy.filters.http.upstream_codec`` terminal filter. The filter
  has no effect in a downstream HTTP filter chain, and is therefore registered only as an upstream
  filter.

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>`

Example configuration
---------------------

The following ``Cluster`` denies any request whose selected upstream IP falls within a private CIDR
range. The upstream filter chain is configured **on the cluster**, under
``typed_extension_protocol_options`` → ``envoy.extensions.upstreams.http.v3.HttpProtocolOptions`` →
``http_filters`` (the same ``HttpProtocolOptions`` entry that carries the upstream protocol config),
and ends with the ``upstream_codec`` terminal filter:

.. code-block:: yaml

  clusters:
  - name: egress_cluster
    connect_timeout: 5s
    type: STRICT_DNS
    load_assignment: {} # ... cluster endpoints ...
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        "@type": type.googleapis.com/envoy.extensions.upstreams.http.v3.HttpProtocolOptions
        # Upstream protocol selection (required by HttpProtocolOptions).
        explicit_http_config:
          http_protocol_options: {}
        # Upstream HTTP filter chain. Must end with envoy.filters.http.upstream_codec.
        http_filters:
        - name: envoy.filters.http.upstream_rbac
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
            rules:
              action: DENY
              policies:
                "deny-private-ips":
                  permissions:
                  - matcher:
                      name: envoy.rbac.matchers.upstream_ip_port
                      typed_config:
                        "@type": type.googleapis.com/envoy.extensions.rbac.matchers.upstream_ip_port.v3.UpstreamIpPortMatcher
                        upstream_ip:
                          address_prefix: 10.0.0.0
                          prefix_len: 8
                  principals:
                  - any: true
        - name: envoy.filters.http.upstream_codec
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.upstream_codec.v3.UpstreamCodec

Statistics
----------

The upstream RBAC filter outputs the same statistics as the
:ref:`RBAC filter <config_http_filters_rbac>`, with the counter names built as
``<scope>.rbac.[<rules_stat_prefix>.]{allowed,denied,shadow_allowed,shadow_denied}``. The
``<scope>`` prefix depends on **where the filter is attached**, not on the filter itself:

* When attached to the router via :ref:`Router.upstream_http_filters
  <envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>`, the stats
  inherit the router's (HTTP connection manager / listener) scope. On a listener with no stats
  prefix this is the **root** scope, so the counters are simply ``rbac.*`` (e.g.
  ``rbac.<rules_stat_prefix>.allowed``). This produces a single aggregate per Envoy, independent of
  which upstream cluster is selected at request time.
* When attached to a cluster via
  :ref:`HttpProtocolOptions.http_filters
  <envoy_v3_api_field_extensions.upstreams.http.v3.HttpProtocolOptions.http_filters>`, the stats are
  emitted in that **cluster's** scope, i.e. ``cluster.<name>.rbac.*``.

.. note::

   With :ref:`dynamic forward proxy <envoy_v3_api_field_extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig.sub_cluster_config>`
   ``sub_cluster_config``, the cluster selected at request time is a per-host dynamic sub-cluster
   (``DFPCluster:<host>:<port>``). Attaching the filter at the **cluster** scope in that mode would
   therefore emit one counter series per resolved upstream host — i.e. unbounded cardinality — and
   scatter deny counts across many series. Prefer attaching the filter at the **router** scope for
   the dynamic forward proxy use case so the counters stay a single root-scoped aggregate.

The optional ``rules_stat_prefix`` / ``shadow_rules_stat_prefix`` fields of the
:ref:`RBAC <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBAC>` config add a further segment
after ``rbac.`` (e.g. ``rules_stat_prefix: private`` yields ``rbac.private.allowed``); this is
orthogonal to the scope prefix above.

Dynamic Metadata
----------------

The upstream RBAC filter emits the same dynamic metadata as the
:ref:`RBAC filter <config_http_filters_rbac_dynamic_metadata>`, under the
``envoy.filters.http.upstream_rbac`` key namespace.

Per-Route Configuration
-----------------------

Like the downstream :ref:`RBAC filter <config_http_filters_rbac>`, the engine can be overridden or
disabled per virtual host / route / weighted cluster by attaching a
:ref:`RBACPerRoute <envoy_v3_api_msg_extensions.filters.http.rbac.v3.RBACPerRoute>` to the route's
``typed_per_filter_config``, keyed by this filter's configured name. The override is resolved over
the request's (downstream) route, so the same route entry can carry distinct downstream and upstream
RBAC overrides.
