#include "test/mocks/upstream/cluster_info.h"

#include <limits>

#include "envoy/common/optref.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/upstream/host_description.h"
#include "envoy/upstream/upstream.h"

#include "source/common/config/metadata.h"
#include "source/common/http/utility.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/router/upstream_codec_filter.h"
#include "source/common/upstream/upstream_impl.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

MockIdleTimeEnabledClusterInfo::MockIdleTimeEnabledClusterInfo() {
  ON_CALL(*this, idleTimeout()).WillByDefault(Return(std::chrono::milliseconds(1000)));
}

MockIdleTimeEnabledClusterInfo::~MockIdleTimeEnabledClusterInfo() = default;

MockUpstreamLocalAddressSelector::MockUpstreamLocalAddressSelector(
    Network::Address::InstanceConstSharedPtr& address)
    : address_(address) {
  ON_CALL(*this, getUpstreamLocalAddressImpl(_))
      .WillByDefault(
          Invoke([&](const Network::Address::InstanceConstSharedPtr&) -> UpstreamLocalAddress {
            UpstreamLocalAddress ret;
            ret.address_ = address_;
            ret.socket_options_ = nullptr;
            return ret;
          }));
}

MockClusterInfo::MockClusterInfo()
    : http2_options_(::Envoy::Http2::Utility::initializeAndValidateOptions(
                         envoy::config::core::v3::Http2ProtocolOptions())
                         .value()),
      traffic_stat_names_(stats_store_.symbolTable()),
      config_update_stats_names_(stats_store_.symbolTable()),
      lb_stat_names_(stats_store_.symbolTable()), endpoint_stat_names_(stats_store_.symbolTable()),
      cluster_load_report_stat_names_(stats_store_.symbolTable()),
      cluster_circuit_breakers_stat_names_(stats_store_.symbolTable()),
      cluster_request_response_size_stat_names_(stats_store_.symbolTable()),
      cluster_timeout_budget_stat_names_(stats_store_.symbolTable()),
      traffic_stats_(
          ClusterInfoImpl::generateStats(stats_store_.rootScope(), traffic_stat_names_, false)),
      config_update_stats_(config_update_stats_names_, *stats_store_.rootScope()),
      lb_stats_(lb_stat_names_, *stats_store_.rootScope()),
      endpoint_stats_(endpoint_stat_names_, *stats_store_.rootScope()),
      transport_socket_matcher_(new NiceMock<Upstream::MockTransportSocketMatcher>()),
      load_report_stats_(ClusterInfoImpl::generateLoadReportStats(
          *load_report_stats_store_.rootScope(), cluster_load_report_stat_names_)),
      request_response_size_stats_(std::make_unique<ClusterRequestResponseSizeStats>(
          ClusterInfoImpl::generateRequestResponseSizeStats(
              *request_response_size_stats_store_.rootScope(),
              cluster_request_response_size_stat_names_))),
      timeout_budget_stats_(
          std::make_unique<ClusterTimeoutBudgetStats>(ClusterInfoImpl::generateTimeoutBudgetStats(
              *timeout_budget_stats_store_.rootScope(), cluster_timeout_budget_stat_names_))),
      circuit_breakers_stats_(ClusterInfoImpl::generateCircuitBreakersStats(
          *stats_store_.rootScope(), cluster_circuit_breakers_stat_names_.default_, true,
          cluster_circuit_breakers_stat_names_)),
      resource_manager_(new Upstream::ResourceManagerImpl(
          runtime_, "fake_key", 1, 1024, 1024, 1, std::numeric_limits<uint64_t>::max(),
          std::numeric_limits<uint64_t>::max(), circuit_breakers_stats_, absl::nullopt,
          absl::nullopt)),
      upstream_local_address_selector_(
          std::make_shared<NiceMock<MockUpstreamLocalAddressSelector>>(source_address_)),
      stats_scope_(stats_store_.createScope("test_scope")) {
  ON_CALL(*this, connectTimeout()).WillByDefault(Return(std::chrono::milliseconds(5001)));
  ON_CALL(*this, idleTimeout()).WillByDefault(Return(absl::optional<std::chrono::milliseconds>()));
  ON_CALL(*this, perUpstreamPreconnectRatio()).WillByDefault(Return(1.0));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, observabilityName()).WillByDefault(ReturnRef(observability_name_));
  ON_CALL(*this, edsServiceName()).WillByDefault(Invoke([this]() -> const std::string& {
    return eds_service_name_.has_value() ? eds_service_name_.value() : Envoy::EMPTY_STRING;
  }));
  ON_CALL(*this, loadBalancerFactory()).WillByDefault(Invoke([this]() -> TypedLoadBalancerFactory& {
    return *lb_factory_;
  }));
  ON_CALL(*this, http1Settings()).WillByDefault(ReturnRef(http1_settings_));
  ON_CALL(*this, http2Options()).WillByDefault(ReturnRef(http2_options_));
  ON_CALL(*this, http3Options()).WillByDefault(ReturnRef(http3_options_));
  ON_CALL(*this, commonHttpProtocolOptions())
      .WillByDefault(ReturnRef(common_http_protocol_options_));
  ON_CALL(*this, extensionProtocolOptions(_)).WillByDefault(Return(extension_protocol_options_));
  ON_CALL(*this, maxResponseHeadersCount())
      .WillByDefault(ReturnPointee(&max_response_headers_count_));
  ON_CALL(*this, maxResponseHeadersKb()).WillByDefault(Invoke([this]() -> absl::optional<uint16_t> {
    if (common_http_protocol_options_.has_max_response_headers_kb()) {
      return common_http_protocol_options_.max_response_headers_kb().value();
    } else {
      return absl::nullopt;
    }
  }));
  ON_CALL(*this, maxRequestsPerConnection())
      .WillByDefault(ReturnPointee(&max_requests_per_connection_));
  ON_CALL(*this, trafficStats()).WillByDefault(ReturnRef(traffic_stats_));
  ON_CALL(*this, lbStats()).WillByDefault(ReturnRef(lb_stats_));
  ON_CALL(*this, configUpdateStats()).WillByDefault(ReturnRef(config_update_stats_));
  ON_CALL(*this, endpointStats()).WillByDefault(ReturnRef(endpoint_stats_));
  ON_CALL(*this, statsScope()).WillByDefault(ReturnRef(*stats_store_.rootScope()));
  // TODO(incfly): The following is a hack because it's not possible to directly embed
  // a mock transport socket factory matcher due to circular dependencies. Fix this up in a follow
  // up.
  ON_CALL(*this, transportSocketMatcher())
      .WillByDefault(
          Invoke([this]() -> TransportSocketMatcher& { return *transport_socket_matcher_; }));
  ON_CALL(*this, loadReportStats()).WillByDefault(ReturnRef(load_report_stats_));
  ON_CALL(*this, requestResponseSizeStats())
      .WillByDefault(Return(
          std::reference_wrapper<ClusterRequestResponseSizeStats>(*request_response_size_stats_)));
  ON_CALL(*this, timeoutBudgetStats())
      .WillByDefault(
          Return(std::reference_wrapper<ClusterTimeoutBudgetStats>(*timeout_budget_stats_)));
  ON_CALL(*this, getUpstreamLocalAddressSelector())
      .WillByDefault(Return(upstream_local_address_selector_));
  ON_CALL(*this, resourceManager(_))
      .WillByDefault(Invoke(
          [this](ResourcePriority) -> Upstream::ResourceManager& { return *resource_manager_; }));
  ON_CALL(*this, upstreamConfig())
      .WillByDefault(
          Invoke([this]() -> OptRef<const envoy::config::core::v3::TypedExtensionConfig> {
            return makeOptRefFromPtr<const envoy::config::core::v3::TypedExtensionConfig>(
                upstream_config_.get());
          }));

  ON_CALL(*this, lbConfig()).WillByDefault(ReturnRef(lb_config_));
  ON_CALL(*this, metadata()).WillByDefault(ReturnRef(metadata_));
  ON_CALL(*this, upstreamHttpProtocolOptions())
      .WillByDefault(ReturnRef(upstream_http_protocol_options_));
  ON_CALL(*this, alternateProtocolsCacheOptions())
      .WillByDefault(ReturnRef(alternate_protocols_cache_options_));
  // Delayed construction of typed_metadata_, to allow for injection of metadata
  ON_CALL(*this, typedMetadata())
      .WillByDefault(Invoke([this]() -> const Envoy::Config::TypedMetadata& {
        if (typed_metadata_ == nullptr) {
          typed_metadata_ =
              std::make_unique<Config::TypedMetadataImpl<ClusterTypedMetadataFactory>>(metadata_);
        }
        return *typed_metadata_;
      }));
  ON_CALL(*this, clusterType())
      .WillByDefault(
          Invoke([this]() -> OptRef<const envoy::config::cluster::v3::Cluster::CustomClusterType> {
            return makeOptRefFromPtr<const envoy::config::cluster::v3::Cluster::CustomClusterType>(
                cluster_type_.get());
          }));
  ON_CALL(*this, upstreamHttpProtocol(_))
      .WillByDefault(Return(std::vector<Http::Protocol>{Http::Protocol::Http11}));
  ON_CALL(*this, createFilterChain(_, _, _))
      .WillByDefault(Invoke([&](Http::FilterChainManager& manager, bool only_create_if_configured,
                                const Http::FilterChainOptions&) -> bool {
        if (only_create_if_configured) {
          return false;
        }
        Http::FilterFactoryCb factory_cb =
            [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
          callbacks.addStreamDecoderFilter(std::make_shared<Router::UpstreamCodecFilter>());
        };
        manager.applyFilterFactoryCb({}, factory_cb);
        return true;
      }));
  ON_CALL(*this, loadBalancerConfig())
      .WillByDefault(Return(makeOptRefFromPtr<const LoadBalancerConfig>(nullptr)));
  ON_CALL(*this, makeHeaderValidator(_)).WillByDefault(Invoke([&](Http::Protocol protocol) {
    return header_validator_factory_ ? header_validator_factory_->createClientHeaderValidator(
                                           protocol, codecStats(protocol))
                                     : nullptr;
  }));
  ON_CALL(*this, lrsReportMetricNames())
      .WillByDefault(Invoke([this]() -> OptRef<const Envoy::Orca::LrsReportMetricNames> {
        return makeOptRefFromPtr<const Envoy::Orca::LrsReportMetricNames>(
            lrs_report_metric_names_.get());
      }));
}

MockClusterInfo::~MockClusterInfo() = default;

::Envoy::Http::HeaderValidatorStats& MockClusterInfo::codecStats(Http::Protocol protocol) const {
  switch (protocol) {
  case ::Envoy::Http::Protocol::Http10:
  case ::Envoy::Http::Protocol::Http11:
    return http1CodecStats();
  case ::Envoy::Http::Protocol::Http2:
    return http2CodecStats();
  case ::Envoy::Http::Protocol::Http3:
    return http3CodecStats();
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

Http::Http1::CodecStats& MockClusterInfo::http1CodecStats() const {
  return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *stats_scope_);
}

Http::Http2::CodecStats& MockClusterInfo::http2CodecStats() const {
  return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *stats_scope_);
}

Http::Http3::CodecStats& MockClusterInfo::http3CodecStats() const {
  return Http::Http3::CodecStats::atomicGet(http3_codec_stats_, *stats_scope_);
}

} // namespace Upstream
} // namespace Envoy
