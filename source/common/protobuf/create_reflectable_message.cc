#include "source/common/protobuf/utility.h"

#if defined(ENVOY_ENABLE_FULL_PROTOS)

namespace Envoy {
Protobuf::ReflectableMessage createReflectableMessage(const Protobuf::Message& message) {
  return const_cast<Protobuf::ReflectableMessage>(&message);
}
} // namespace Envoy

#else

#include "envoy/config/bootstrap/v3/bootstrap_descriptor.pb.h"
#include "envoy/config/route/v3/route_components_descriptor.pb.h"
#include "envoy/config/upstream/local_address_selector/v3/default_local_address_selector_descriptor.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster_descriptor.pb.h"
#include "envoy/extensions/compression/brotli/decompressor/v3/brotli_descriptor.pb.h"
#include "envoy/extensions/compression/gzip/decompressor/v3/gzip_descriptor.pb.h"
#include "envoy/extensions/early_data/v3/default_early_data_policy_descriptor.pb.h"
#include "envoy/extensions/filters/http/alternate_protocols_cache/v3/alternate_protocols_cache_descriptor.pb.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer_descriptor.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor_descriptor.pb.h"
#include "envoy/extensions/filters/http/dynamic_forward_proxy/v3/dynamic_forward_proxy_descriptor.pb.h"
#include "envoy/extensions/filters/http/router/v3/router_descriptor.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec_descriptor.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager_descriptor.pb.h"
#include "envoy/extensions/http/header_formatters/preserve_case/v3/preserve_case_descriptor.pb.h"
#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator_descriptor.pb.h"
#include "envoy/extensions/http/original_ip_detection/xff/v3/xff_descriptor.pb.h"
#include "envoy/extensions/load_balancing_policies/cluster_provided/v3/cluster_provided_descriptor.pb.h"
#include "envoy/extensions/network/dns_resolver/getaddrinfo/v3/getaddrinfo_dns_resolver_descriptor.pb.h"
#include "envoy/extensions/network/socket_interface/v3/default_socket_interface_descriptor.pb.h"
#include "envoy/extensions/path/match/uri_template/v3/uri_template_match_descriptor.pb.h"
#include "envoy/extensions/path/rewrite/uri_template/v3/uri_template_rewrite_descriptor.pb.h"
#include "envoy/extensions/quic/client_writer_factory/v3/default_client_writer_descriptor.pb.h"
#include "envoy/extensions/regex_engines/v3/google_re2_descriptor.pb.h"
#include "envoy/extensions/request_id/uuid/v3/uuid_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/raw_buffer/v3/raw_buffer_descriptor.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/tls_descriptor.pb.h"
#include "envoy/extensions/upstreams/http/generic/v3/generic_connection_pool_descriptor.pb.h"
#include "envoy/extensions/upstreams/http/v3/http_protocol_options_descriptor.pb.h"
#include "envoy/type/matcher/v3/http_inputs_descriptor.pb.h"
#include "envoy/watchdog/v3/abort_action_descriptor.pb.h"
#ifdef __APPLE__
#include "envoy/extensions/network/dns_resolver/apple/v3/apple_dns_resolver_descriptor.pb.h"
#endif
#ifdef ENVOY_ADMIN_FUNCTIONALITY
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
#endif
#ifdef ENVOY_MOBILE_ENABLE_LISTENER
#include "envoy/extensions/quic/connection_id_generator/v3/envoy_deterministic_connection_id_generator_descriptor.pb.h"
#include "envoy/extensions/quic/crypto_stream/v3/crypto_stream_descriptor.pb.h"
#include "envoy/extensions/quic/proof_source/v3/proof_source_descriptor.pb.h"
#include "envoy/extensions/udp_packet_writer/v3/udp_default_writer_factory_descriptor.pb.h"
#endif
#ifdef ENVOY_MOBILE_XDS
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
#endif

#include "bazel/cc_proto_descriptor_library/create_dynamic_message.h"
#include "bazel/cc_proto_descriptor_library/file_descriptor_info.h"
#include "bazel/cc_proto_descriptor_library/text_format_transcoder.h"

using cc_proto_descriptor_library::TextFormatTranscoder;
using cc_proto_descriptor_library::internal::FileDescriptorInfo;

namespace {

std::unique_ptr<TextFormatTranscoder> createTranscoder() {
  auto transcoder = std::make_unique<TextFormatTranscoder>(/*allow_global_fallback=*/false);
  // Descriptors for the messages that lite-proto (mobile) builds reflect: the bootstrap, the
  // route match actions, and the config protos of the extensions that Envoy Mobile registers
  // (see mobile/envoy_build_config/extension_registry.cc), plus the watchdog abort action, which is
  // registered wherever the server's guard dog is linked. Loading a file also loads the files it
  // imports, so those don't need to be listed. Descriptors for functionality that mobile
  // builds compile out are only listed when it is enabled, so that their data isn't linked.
  std::vector<FileDescriptorInfo> file_descriptors = {
      protobuf::reflection::envoy_config_bootstrap_v3_bootstrap::kFileDescriptorInfo,
      protobuf::reflection::envoy_config_route_v3_route_components::kFileDescriptorInfo,
      protobuf::reflection::
          envoy_config_upstream_local_address_selector_v3_default_local_address_selector::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_clusters_dynamic_forward_proxy_v3_cluster::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_compression_brotli_decompressor_v3_brotli::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_compression_gzip_decompressor_v3_gzip::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_early_data_v3_default_early_data_policy::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_filters_http_alternate_protocols_cache_v3_alternate_protocols_cache::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_buffer_v3_buffer::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_decompressor_v3_decompressor::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_filters_http_dynamic_forward_proxy_v3_dynamic_forward_proxy::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_router_v3_router::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_filters_http_upstream_codec_v3_upstream_codec::
          kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_filters_network_http_connection_manager_v3_http_connection_manager::
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
      protobuf::reflection::
          envoy_extensions_network_dns_resolver_getaddrinfo_v3_getaddrinfo_dns_resolver::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_network_socket_interface_v3_default_socket_interface::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_path_match_uri_template_v3_uri_template_match::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_path_rewrite_uri_template_v3_uri_template_rewrite::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_quic_client_writer_factory_v3_default_client_writer::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_regex_engines_v3_google_re2::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_request_id_uuid_v3_uuid::kFileDescriptorInfo,
      protobuf::reflection::
          envoy_extensions_transport_sockets_http_11_proxy_v3_upstream_http_11_connect::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_quic_v3_quic_transport::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_raw_buffer_v3_raw_buffer::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_transport_sockets_tls_v3_tls::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_upstreams_http_generic_v3_generic_connection_pool::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_upstreams_http_v3_http_protocol_options::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_type_matcher_v3_http_inputs::kFileDescriptorInfo,
      protobuf::reflection::envoy_watchdog_v3_abort_action::kFileDescriptorInfo,
#ifdef __APPLE__
      protobuf::reflection::envoy_extensions_network_dns_resolver_apple_v3_apple_dns_resolver::
          kFileDescriptorInfo,
#endif
#ifdef ENVOY_ADMIN_FUNCTIONALITY
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
#endif
#ifdef ENVOY_MOBILE_ENABLE_LISTENER
      protobuf::reflection::
          envoy_extensions_quic_connection_id_generator_v3_envoy_deterministic_connection_id_generator::
              kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_quic_crypto_stream_v3_crypto_stream::
          kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_quic_proof_source_v3_proof_source::kFileDescriptorInfo,
      protobuf::reflection::envoy_extensions_udp_packet_writer_v3_udp_default_writer_factory::
          kFileDescriptorInfo,
#endif
#ifdef ENVOY_MOBILE_XDS
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
#endif
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
  ASSERT(reflectable_message,
         absl::StrCat("Unable to create dynamic message for: ", message.GetTypeName()));
  return reflectable_message;
}

} // namespace Envoy
#endif
