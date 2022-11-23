package test.kotlin.integration.proxying

import android.content.Context

import io.envoyproxy.envoymobile.AndroidEngineBuilder
import io.envoyproxy.envoymobile.Custom
import io.envoyproxy.envoymobile.EngineBuilder

import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

// A convenient wrapper for creating an builder for an engine that
// proxies network requests.
class Proxy constructor(val context: Context, val port: Int) {
    fun http(): EngineBuilder {
      val config = """
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 127.0.0.1, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
        config:
          stat_prefix: api_hcm
          route_config:
            name: api_router
            virtual_hosts:
            - name: api
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                direct_response: { status: 400, body: { inline_string: "not found" } }
  - name: listener_proxy
    address:
      socket_address: { address: 127.0.0.1, port_value: $port }
    filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: remote_hcm
            route_config:
              name: remote_route
              virtual_hosts:
              - name: remote_service
                domains: ["*"]
                routes:
                - match: { prefix: "/" }
                  route: { cluster: cluster_proxy }
              response_headers_to_add:
                - append_action: OVERWRITE_IF_EXISTS_OR_ADD
                  header:
                    key: x-proxy-response
                    value: 'true'
            http_filters:
              - name: envoy.filters.http.local_error
                typed_config:
                  "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError
              - name: envoy.filters.http.dynamic_forward_proxy
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
                  dns_cache_config: &dns_cache_config
                    name: base_dns_cache
                    dns_lookup_family: ALL
                    host_ttl: 86400s
                    dns_min_refresh_rate: 20s
                    dns_refresh_rate: 60s
                    dns_failure_refresh_rate:
                      base_interval: 2s
                      max_interval: 10s
                    dns_query_timeout: 25s
                    typed_dns_resolver_config:
                      name: envoy.network.dns_resolver.getaddrinfo
                      typed_config: {"@type":"type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig"}
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: cluster_proxy
    connect_timeout: 30s
    lb_policy: CLUSTER_PROVIDED
    dns_lookup_family: ALL
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
layered_runtime:
  layers:
    - name: static_layer_0
      static_layer:
        envoy:
          # This disables envoy bug stats, which are filtered out of our stats inclusion list anyway
          # Global stats do not play well with engines with limited lifetimes
          disallow_global_stats: true
"""
      return AndroidEngineBuilder(context, Custom(config))
    }

    fun https(): EngineBuilder {
      val config = """
static_resources:
  listeners:
  - name: base_api_listener
    address:
      socket_address: { protocol: TCP, address: 127.0.0.1, port_value: 10000 }
    api_listener:
      api_listener:
        "@type": "type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.EnvoyMobileHttpConnectionManager"
        config:
          stat_prefix: api_hcm
          route_config:
            name: api_router
            virtual_hosts:
            - name: api
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                direct_response: { status: 400, body: { inline_string: "not found" } }
  - name: listener_proxy
    address:
      socket_address: { address: 127.0.0.1, port_value: $port }
    filter_chains:
      - filters:
        - name: envoy.filters.network.http_connection_manager
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
            stat_prefix: remote_hcm
            route_config:
              name: remote_route
              virtual_hosts:
              - name: remote_service
                domains: ["*"]
                routes:
                - match: { connect_matcher: {} }
                  route:
                    cluster: cluster_proxy
                    upgrade_configs:
                    - upgrade_type: CONNECT
                      connect_config:
              response_headers_to_add:
                - append_action: OVERWRITE_IF_EXISTS_OR_ADD
                  header:
                    key: x-response-header-that-should-be-stripped
                    value: 'true'
            http_filters:
              - name: envoy.filters.http.local_error
                typed_config:
                  "@type": type.googleapis.com/envoymobile.extensions.filters.http.local_error.LocalError
              - name: envoy.filters.http.dynamic_forward_proxy
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_forward_proxy.v3.FilterConfig
                  dns_cache_config: &dns_cache_config
                    name: base_dns_cache
                    dns_lookup_family: ALL
                    host_ttl: 86400s
                    dns_min_refresh_rate: 20s
                    dns_refresh_rate: 60s
                    dns_failure_refresh_rate:
                      base_interval: 2s
                      max_interval: 10s
                    dns_query_timeout: 25s
                    typed_dns_resolver_config:
                      name: envoy.network.dns_resolver.getaddrinfo
                      typed_config: {"@type":"type.googleapis.com/envoy.extensions.network.dns_resolver.getaddrinfo.v3.GetAddrInfoDnsResolverConfig"}
              - name: envoy.router
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  clusters:
  - name: cluster_proxy
    connect_timeout: 30s
    lb_policy: CLUSTER_PROVIDED
    dns_lookup_family: ALL
    cluster_type:
      name: envoy.clusters.dynamic_forward_proxy
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.clusters.dynamic_forward_proxy.v3.ClusterConfig
        dns_cache_config: *dns_cache_config
layered_runtime:
  layers:
    - name: static_layer_0
      static_layer:
        envoy:
          # This disables envoy bug stats, which are filtered out of our stats inclusion list anyway
          # Global stats do not play well with engines with limited lifetimes
          disallow_global_stats: true
"""
      return AndroidEngineBuilder(context, Custom(config))
    }
}
