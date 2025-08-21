#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/network/ext_authz/v3/ext_authz.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/runtime/runtime.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/matchers.h"
#include "source/extensions/filters/common/ext_authz/ext_authz.h"
#include "source/extensions/filters/common/ext_authz/ext_authz_grpc_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtAuthz {

/**
 * All tcp external authorization stats. @see stats_macros.h
 */
#define ALL_TCP_EXT_AUTHZ_STATS(COUNTER, GAUGE)                                                    \
  COUNTER(cx_closed)                                                                               \
  COUNTER(denied)                                                                                  \
  COUNTER(error)                                                                                   \
  COUNTER(failure_mode_allowed)                                                                    \
  COUNTER(ok)                                                                                      \
  COUNTER(total)                                                                                   \
  COUNTER(disabled)                                                                                \
  GAUGE(active, Accumulate)

/**
 * Struct definition for all external authorization stats. @see stats_macros.h
 */
struct InstanceStats {
  ALL_TCP_EXT_AUTHZ_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Global configuration for ExtAuthz filter.
 */
class Config {
  using LabelsMap = Protobuf::Map<std::string, std::string>;

public:
  Config(const envoy::extensions::filters::network::ext_authz::v3::ExtAuthz& config,
         Stats::Scope& scope, Server::Configuration::ServerFactoryContext& context)
      : stats_(generateStats(config.stat_prefix(), scope)),
        failure_mode_allow_(config.failure_mode_allow()),
        include_peer_certificate_(config.include_peer_certificate()),
        include_tls_session_(config.include_tls_session()),
        filter_enabled_metadata_(
            config.has_filter_enabled_metadata()
                ? absl::optional<Matchers::MetadataMatcher>(
                      Matchers::MetadataMatcher(config.filter_enabled_metadata(), context))
                : absl::nullopt) {
    auto labels_key_it =
        context.bootstrap().node().metadata().fields().find(config.bootstrap_metadata_labels_key());
    if (labels_key_it != context.bootstrap().node().metadata().fields().end()) {
      for (const auto& labels_it : labels_key_it->second.struct_value().fields()) {
        destination_labels_[labels_it.first] = labels_it.second.string_value();
      }
    }
  }

  const InstanceStats& stats() { return stats_; }
  bool failureModeAllow() const { return failure_mode_allow_; }
  void setFailModeAllow(bool value) { failure_mode_allow_ = value; }
  bool includePeerCertificate() const { return include_peer_certificate_; }
  bool includeTLSSession() const { return include_tls_session_; }
  const LabelsMap& destinationLabels() const { return destination_labels_; }
  bool filterEnabledMetadata(const envoy::config::core::v3::Metadata& metadata) const {
    return filter_enabled_metadata_.has_value() ? filter_enabled_metadata_->match(metadata) : true;
  }

private:
  static InstanceStats generateStats(const std::string& name, Stats::Scope& scope);
  const InstanceStats stats_;
  bool failure_mode_allow_;
  LabelsMap destination_labels_;
  const bool include_peer_certificate_;
  const bool include_tls_session_;
  const absl::optional<Matchers::MetadataMatcher> filter_enabled_metadata_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

/**
 * ExtAuthz filter instance. This filter will call the Authorization service with the given
 * configuration parameters. If the authorization service returns an error or a deny the
 * connection will be closed without any further filters being called. Otherwise all buffered
 * data will be released to further filters.
 */
class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public Filters::Common::ExtAuthz::RequestCallbacks {
  using LabelsMap = std::map<std::string, std::string>;

public:
  Filter(ConfigSharedPtr config, Filters::Common::ExtAuthz::ClientPtr&& client)
      : config_(config), client_(std::move(client)) {}
  ~Filter() override = default;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override {
    filter_callbacks_ = &callbacks;
    filter_callbacks_->connection().addConnectionCallbacks(*this);
  }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // ExtAuthz::RequestCallbacks
  void onComplete(Filters::Common::ExtAuthz::ResponsePtr&&) override;

private:
  // State of this filter's communication with the external authorization service.
  // The filter has either not started calling the external service, in the middle of calling
  // it or has completed.
  enum class Status { NotStarted, Calling, Complete };
  // FilterReturn is used to capture what the return code should be to the filter chain.
  // if this filter is either in the middle of calling the external service or the result is denied
  // then the filter chain should stop. Otherwise the filter chain can continue to the next filter.
  enum class FilterReturn { Stop, Continue };
  void callCheck();

  bool filterEnabled(const envoy::config::core::v3::Metadata& metadata) {
    return config_->filterEnabledMetadata(metadata);
  }
  absl::optional<MonotonicTime> start_time_;

  ConfigSharedPtr config_;
  Filters::Common::ExtAuthz::ClientPtr client_;
  Network::ReadFilterCallbacks* filter_callbacks_{};
  Status status_{Status::NotStarted};
  FilterReturn filter_return_{FilterReturn::Stop};
  // Used to identify if the callback to onComplete() is synchronous (on the stack) or asynchronous.
  bool calling_check_{};
  envoy::service::auth::v3::CheckRequest check_request_{};
};
} // namespace ExtAuthz
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
