#include "source/common/protobuf/utility.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace Envoy {
Protobuf::ReflectableMessage createReflectableMessage(const Protobuf::Message& message) {
  return const_cast<Protobuf::ReflectableMessage>(&message);
}
} // namespace Envoy

#else

#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"
#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"

#include "envoy/config/core/v3/base_descriptor.pb.h"
#include "envoy/admin/v3/certs_descriptor.pb.h"
#include "envoy/admin/v3/clusters_descriptor.pb.h"
#include "envoy/admin/v3/config_dump_descriptor.pb.h"
#include "envoy/admin/v3/config_dump_shared_descriptor.pb.h"
#include "envoy/admin/v3/init_dump_descriptor.pb.h"
#include "envoy/admin/v3/listeners_descriptor.pb.h"
#include "envoy/admin/v3/memory_descriptor.pb.h"
#include "envoy/admin/v3/metrics_descriptor.pb.h"
#include "envoy/admin/v3/mutex_stats_descriptor.pb.h"
#include "envoy/admin/v3/server_info_descriptor.pb.h"
#include "envoy/admin/v3/tap_descriptor.pb.h"
#include "envoy/annotations/deprecation_descriptor.pb.h"
#include "envoy/annotations/resource_descriptor.pb.h"
#include "envoy/config/accesslog/v3/accesslog_descriptor.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap_descriptor.pb.h"
#include "envoy/config/cluster/v3/circuit_breaker_descriptor.pb.h"
#include "envoy/config/cluster/v3/cluster_descriptor.pb.h"
#include "envoy/config/cluster/v3/filter_descriptor.pb.h"
#include "envoy/config/cluster/v3/outlier_detection_descriptor.pb.h"
#include "envoy/config/common/key_value/v3/config_descriptor.pb.h"
#include "envoy/config/common/matcher/v3/matcher_descriptor.pb.h"
#include "envoy/config/core/v3/address_descriptor.pb.h"
#include "envoy/config/core/v3/backoff_descriptor.pb.h"
#include "envoy/config/core/v3/base_descriptor.pb.h"
#include "envoy/config/core/v3/config_source_descriptor.pb.h"
#include "envoy/config/core/v3/event_service_config_descriptor.pb.h"
#include "envoy/config/core/v3/extension_descriptor.pb.h"
#include "envoy/config/core/v3/grpc_method_list_descriptor.pb.h"
#include "envoy/config/core/v3/grpc_service_descriptor.pb.h"
#include "envoy/config/core/v3/health_check_descriptor.pb.h"
#include "envoy/config/core/v3/http_uri_descriptor.pb.h"
#include "envoy/config/core/v3/protocol_descriptor.pb.h"
#include "envoy/config/core/v3/proxy_protocol_descriptor.pb.h"
#include "envoy/config/core/v3/resolver_descriptor.pb.h"
#include "envoy/config/core/v3/socket_option_descriptor.pb.h"
#include "envoy/config/core/v3/substitution_format_string_descriptor.pb.h"
#include "envoy/config/core/v3/udp_socket_config_descriptor.pb.h"
#include "envoy/config/endpoint/v3/endpoint_descriptor.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components_descriptor.pb.h"
#include "envoy/config/endpoint/v3/load_report_descriptor.pb.h"
#include "envoy/config/listener/v3/api_listener_descriptor.pb.h"
#include "envoy/config/listener/v3/listener_descriptor.pb.h"
#include "envoy/config/listener/v3/listener_components_descriptor.pb.h"
#include "envoy/config/listener/v3/quic_config_descriptor.pb.h"
#include "envoy/config/listener/v3/udp_listener_config_descriptor.pb.h"
#include "envoy/config/metrics/v3/metrics_service_descriptor.pb.h"
#include "envoy/config/metrics/v3/stats_descriptor.pb.h"
#include "envoy/config/overload/v3/overload_descriptor.pb.h"
#include "envoy/config/route/v3/route_descriptor.pb.h"
#include "envoy/config/route/v3/route_components_descriptor.pb.h"
#include "envoy/config/route/v3/scoped_route_descriptor.pb.h"
#include "envoy/config/trace/v3/datadog_descriptor.pb.h"
#include "envoy/config/trace/v3/dynamic_ot_descriptor.pb.h"
#include "envoy/config/trace/v3/http_tracer_descriptor.pb.h"
#include "envoy/config/trace/v3/lightstep_descriptor.pb.h"
#include "envoy/config/trace/v3/opencensus_descriptor.pb.h"
#include "envoy/config/trace/v3/opentelemetry_descriptor.pb.h"
#include "envoy/config/trace/v3/service_descriptor.pb.h"
#include "envoy/config/trace/v3/skywalking_descriptor.pb.h"
#include "envoy/config/trace/v3/trace_descriptor.pb.h"
#include "envoy/config/trace/v3/xray_descriptor.pb.h"
#include "envoy/config/trace/v3/zipkin_descriptor.pb.h"
#include "envoy/config/upstream/local_address_selector/v3/default_local_address_selector_descriptor.pb.h"
#include "envoy/data/accesslog/v3/accesslog_descriptor.pb.h"
#include "envoy/data/cluster/v3/outlier_detection_event_descriptor.pb.h"
#include "envoy/data/core/v3/health_check_event_descriptor.pb.h"
#include "envoy/extensions/access_loggers/file/v3/file_descriptor.pb.h"
#include "envoy/extensions/access_loggers/stream/v3/stream_descriptor.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster_descriptor.pb.h"
#include "envoy/extensions/common/dynamic_forward_proxy/v3/dns_cache_descriptor.pb.h"
#include "envoy/extensions/common/matching/v3/extension_matcher_descriptor.pb.h"
#include "envoy/extensions/compression/brotli/compressor/v3/brotli_descriptor.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli_descriptor.pb.h"
#include "envoy/extensions/compression/gzip/compressor/v3/gzip_descriptor.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip_descriptor.pb.h"
#include "envoy/extensions/early_data/v3/default_early_data_policy_descriptor.pb.h"
#include "envoy/extensions/filters/common/dependency/v3/dependency_descriptor.pb.h"
#include "envoy/extensions/filters/common/matcher/action/v3/skip_action_descriptor.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache_descriptor.pb.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer_descriptor.pb.h"
#include "envoy/extensions/filters/http/composite/v3/composite_descriptor.pb.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor_descriptor.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor_descriptor.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy_descriptor.pb.h"
#include "envoy/extensions/filters/http/health_check/v3/health_check_descriptor.pb.h"
#include "envoy/extensions/filters/http/on_demand/v3/on_demand_descriptor.pb.h"
#include "envoy/extensions/filters/http/router/v3/router_descriptor.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec_descriptor.pb.h"
#include "envoy/extensions/filters/listener/proxy_protocol/v3/proxy_protocol_descriptor.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager_descriptor.pb.h"
#include "envoy/extensions/filters/udp/dns_filter/v3/dns_filter_descriptor.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case_descriptor.pb.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator_descriptor.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff_descriptor.pb.h"
#include "envoy/extensions/load_balancing_policies/common/v3/common_descriptor.pb.h"
#include "envoy/extensions/load_balancing_policies/cluster_provided/v3/cluster_provided_descriptor.pb.h"
#include "envoy/extensions/load_balancing_policies/least_request/v3/least_request_descriptor.pb.h"
#include "envoy/extensions/load_balancing_policies/random/v3/random_descriptor.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin_descriptor.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs_descriptor.pb.h"
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver_descriptor.pb.h"
#include "envoy/extensions/network/dns_resolver/cares/v3/cares_dns_resolver_descriptor.pb.h"
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver_descriptor.pb.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface_descriptor.pb.h"
#include "envoy/extensions/path/match/uri_template/v3/uri_template_match_descriptor.pb.h"
#include "envoy/extensions/path/rewrite/uri_template/v3/uri_template_rewrite_descriptor.pb.h"
#include "envoy/extensions/quic/connection_id_generator/v3/envoy_deterministic_connection_id_generator_descriptor.pb.h"
#include "envoy/extensions/quic/crypto_stream/v3/crypto_stream_descriptor.pb.h"
#include "envoy/extensions/quic/proof_source/v3/proof_source_descriptor.pb.h"
#include "envoy/extensions/regex_engines/v3/google_re2_descriptor.pb.h"
#include "envoy/extensions/request_id/uuid/v3/uuid_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/common_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/secret_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config_descriptor.pb.h"
#include "envoy/extensions/udp_packet_writer/v3/udp_default_writer_factory_descriptor.pb.h"
#include "envoy/extensions/udp_packet_writer/v3/udp_gso_batch_writer_factory_descriptor.pb.h"
#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool_descriptor.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options_descriptor.pb.h"
#include "envoy/extensions/upstreams/tcp/v3/tcp_protocol_options_descriptor.pb.h"
#include "envoy/service/cluster/v3/cds_descriptor.pb.h"
#include "envoy/service/discovery/v3/ads_descriptor.pb.h"
#include "envoy/service/discovery/v3/discovery_descriptor.pb.h"
#include "envoy/service/endpoint/v3/eds_descriptor.pb.h"
#include "envoy/service/endpoint/v3/leds_descriptor.pb.h"
#include "envoy/service/extension/v3/config_discovery_descriptor.pb.h"
#include "envoy/service/health/v3/hds_descriptor.pb.h"
#include "envoy/service/listener/v3/lds_descriptor.pb.h"
#include "envoy/service/load_stats/v3/lrs_descriptor.pb.h"
#include "envoy/service/metrics/v3/metrics_service_descriptor.pb.h"
#include "envoy/service/ratelimit/v3/rls_descriptor.pb.h"
#include "envoy/service/route/v3/rds_descriptor.pb.h"
#include "envoy/service/route/v3/srds_descriptor.pb.h"
#include "envoy/service/runtime/v3/rtds_descriptor.pb.h"
#include "envoy/service/secret/v3/sds_descriptor.pb.h"
#include "envoy/type/matcher/metadata_descriptor.pb.h"
#include "envoy/type/matcher/node_descriptor.pb.h"
#include "envoy/type/matcher/number_descriptor.pb.h"
#include "envoy/type/matcher/path_descriptor.pb.h"
#include "envoy/type/matcher/regex_descriptor.pb.h"
#include "envoy/type/matcher/string_descriptor.pb.h"
#include "envoy/type/matcher/struct_descriptor.pb.h"
#include "envoy/type/matcher/value_descriptor.pb.h"
#include "envoy/type/matcher/v3/filter_state_descriptor.pb.h"
#include "envoy/type/matcher/v3/http_inputs_descriptor.pb.h"
#include "envoy/type/matcher/v3/metadata_descriptor.pb.h"
#include "envoy/type/matcher/v3/node_descriptor.pb.h"
#include "envoy/type/matcher/v3/number_descriptor.pb.h"
#include "envoy/type/matcher/v3/path_descriptor.pb.h"
#include "envoy/type/matcher/v3/regex_descriptor.pb.h"
#include "envoy/type/matcher/v3/status_code_input_descriptor.pb.h"
#include "envoy/type/matcher/v3/string_descriptor.pb.h"
#include "envoy/type/matcher/v3/struct_descriptor.pb.h"
#include "envoy/type/matcher/v3/value_descriptor.pb.h"
#include "envoy/type/metadata/v3/metadata_descriptor.pb.h"
#include "envoy/type/tracing/v3/custom_tag_descriptor.pb.h"
#include "envoy/type/v3/hash_policy_descriptor.pb.h"
#include "envoy/type/v3/http_descriptor.pb.h"
#include "envoy/type/v3/http_status_descriptor.pb.h"
#include "envoy/type/v3/percent_descriptor.pb.h"
#include "envoy/type/v3/range_descriptor.pb.h"
#include "envoy/type/v3/ratelimit_strategy_descriptor.pb.h"
#include "envoy/type/v3/ratelimit_unit_descriptor.pb.h"
#include "envoy/type/v3/semantic_version_descriptor.pb.h"
#include "envoy/type/v3/token_bucket_descriptor.pb.h"
#include "envoy/watchdog/v3/abort_action_descriptor.pb.h"

using cc_proto_descriptor_library::TextFormatTranscoder;
using cc_proto_descriptor_library::internal::FileDescriptorInfo;

namespace {

std::unique_ptr<TextFormatTranscoder> createTranscoder() {
  auto transcoder = std::make_unique<TextFormatTranscoder>(/*allow_global_fallback=*/false);
  std::vector<FileDescriptorInfo> file_descriptors = {
      protobuf::reflection::envoy_config_core_v3_base::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_certs::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_clusters::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_config_dump::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_config_dump_shared::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_init_dump::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_listeners::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_memory::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_metrics::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_mutex_stats::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_server_info::kFileDescriptorInfo,
      protobuf::reflection::envoy_admin_v3_tap::kFileDescriptorInfo,
      protobuf::reflection::envoy_annotations_deprecation::kFileDescriptorInfo,
      protobuf::reflection::envoy_annotations_resource::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_accesslog_v3_accesslog::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_bootstrap_v3_bootstrap::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_cluster_v3_circuit_breaker::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_cluster_v3_cluster::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_cluster_v3_filter::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_cluster_v3_outlier_detection::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_common_key_value_v3_config::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_common_matcher_v3_matcher::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_address::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_backoff::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_base::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_config_source::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_event_service_config::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_extension::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_grpc_method_list::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_grpc_service::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_health_check::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_http_uri::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_protocol::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_proxy_protocol::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_resolver::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_socket_option::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_substitution_format_string::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_core_v3_udp_socket_config::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_endpoint_v3_endpoint::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_endpoint_v3_endpoint_components::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_endpoint_v3_load_report::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_listener_v3_api_listener::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_listener_v3_listener::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_listener_v3_listener_components::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_listener_v3_quic_config::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_listener_v3_udp_listener_config::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_metrics_v3_metrics_service::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_metrics_v3_stats::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_overload_v3_overload::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_route_v3_route::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_route_v3_route_components::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_route_v3_scoped_route::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_datadog::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_dynamic_ot::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_http_tracer::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_lightstep::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_opencensus::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_opentelemetry::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_service::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_skywalking::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_trace::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_xray::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_trace_v3_zipkin::kFileDescriptorInfo,
      protobuf::reflection::
          envoy_config_upstream_local_address_selector_v3_default_local_address_selector::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_data_accesslog_v3_accesslog::kFileDescriptorInfo,
      protobuf::reflection::envoy_data_cluster_v3_outlier_detection_event::kFileDescriptorInfo,
      protobuf::reflection::envoy_data_core_v3_health_check_event::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_access_loggers_file_v3_file::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_access_loggers_stream_v3_stream::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_clusters_dynamic_forward_proxy_v3_cluster::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_common_dynamic_forward_proxy_v3_dns_cache::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_common_matching_v3_extension_matcher::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_compression_brotli_compressor_v3_brotli::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_compression_brotli_decompressor_v3_brotli::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_compression_gzip_compressor_v3_gzip::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_compression_gzip_decompressor_v3_gzip::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_early_data_v3_default_early_data_policy::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_common_dependency_v3_dependency::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_common_matcher_action_v3_skip_action::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_filters_http_alternate_protocols_cache_v3_alternate_protocols_cache::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_buffer_v3_buffer::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_composite_v3_composite::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_compressor_v3_compressor::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_decompressor_v3_decompressor::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_filters_http_dynamic_forward_proxy_v3_dynamic_forward_proxy::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_health_check_v3_health_check::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_on_demand_v3_on_demand::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_router_v3_router::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_upstream_codec_v3_upstream_codec::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_listener_proxy_protocol_v3_proxy_protocol::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_filters_network_http_connection_manager_v3_http_connection_manager::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_udp_dns_filter_v3_dns_filter::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_http_header_formatters_preserve_case_v3_preserve_case::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_http_header_validators_envoy_default_v3_header_validator::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_http_original_ip_detection_xff_v3_xff::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_load_balancing_policies_cluster_provided_v3_cluster_provided::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_load_balancing_policies_common_v3_common::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_load_balancing_policies_least_request_v3_least_request::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_load_balancing_policies_random_v3_random::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_load_balancing_policies_round_robin_v3_round_robin::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_matching_common_inputs_network_v3_network_inputs::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_network_dns_resolver_apple_v3_apple_dns_resolver::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_network_dns_resolver_cares_v3_cares_dns_resolver::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_network_dns_resolver_getaddrinfo_v3_getaddrinfo_dns_resolver::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_network_socket_interface_v3_default_socket_interface::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_path_match_uri_template_v3_uri_template_match::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_path_rewrite_uri_template_v3_uri_template_rewrite::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_quic_connection_id_generator_v3_envoy_deterministic_connection_id_generator::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_quic_crypto_stream_v3_crypto_stream::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_quic_proof_source_v3_proof_source::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_regex_engines_v3_google_re2::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_request_id_uuid_v3_uuid::kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_transport_sockets_http_11_proxy_v3_upstream_http_11_connect::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_quic_v3_quic_transport::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_raw_buffer_v3_raw_buffer::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_tls_v3_cert::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_tls_v3_common::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_tls_v3_secret::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_tls_v3_tls::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_tls_v3_tls_spiffe_validator_config::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_udp_packet_writer_v3_udp_default_writer_factory::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_udp_packet_writer_v3_udp_gso_batch_writer_factory::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_upstreams_http_generic_v3_generic_connection_pool::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_upstreams_http_v3_http_protocol_options::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_upstreams_tcp_v3_tcp_protocol_options::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_service_cluster_v3_cds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_discovery_v3_ads::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_discovery_v3_discovery::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_endpoint_v3_eds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_endpoint_v3_leds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_extension_v3_config_discovery::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_health_v3_hds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_listener_v3_lds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_load_stats_v3_lrs::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_metrics_v3_metrics_service::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_ratelimit_v3_rls::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_route_v3_rds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_route_v3_srds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_runtime_v3_rtds::kFileDescriptorInfo,
      protobuf::reflection::envoy_service_secret_v3_sds::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_metadata::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_node::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_number::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_path::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_regex::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_string::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_struct::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_value::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_filter_state::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_http_inputs::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_metadata::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_node::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_number::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_path::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_regex::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_status_code_input::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_string::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_struct::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_value::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_metadata_v3_metadata::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_tracing_v3_custom_tag::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_hash_policy::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_http::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_http_status::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_percent::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_range::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_ratelimit_strategy::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_ratelimit_unit::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_semantic_version::kFileDescriptorInfo,
      protobuf::reflection::envoy_type_v3_token_bucket::kFileDescriptorInfo,
      protobuf::reflection::envoy_watchdog_v3_abort_action::kFileDescriptorInfo,
  };
  for (const FileDescriptorInfo& descriptor : file_descriptors) {
    transcoder->loadFileDescriptors(descriptor);
  }
  return transcoder;
}

TextFormatTranscoder& getTranscoder() {
  // This transcoder is used by createDynamicMessage() to convert an instance of a MessageLite
  // subclass into an instance of Message. This Message instance has reflection capabilities
  // but does not have the per-field accessors that the generated C++ subclasses have.
  // As such it is only useful for reflection uses.
  static std::unique_ptr<TextFormatTranscoder> transcoder = createTranscoder();
  return *transcoder;
}

} // namespace

namespace Envoy {

void loadFileDescriptors(const FileDescriptorInfo& file_descriptor_info) {
  getTranscoder().loadFileDescriptors(file_descriptor_info);
}

Protobuf::ReflectableMessage createReflectableMessage(const Protobuf::Message& message) {
  Protobuf::ReflectableMessage reflectable_message = createDynamicMessage(getTranscoder(), message);
  ASSERT(reflectable_message, "Unable to create dyanmic message for: " + message.GetTypeName());
  return reflectable_message;
}

} // namespace Envoy
#endif
