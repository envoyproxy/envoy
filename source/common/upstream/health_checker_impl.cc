#include "source/common/upstream/health_checker_impl.h"

#include <cstdint>
#include <iterator>
#include <memory>

#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

#include "source/common/buffer/zero_copy_input_stream_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/enum_to_int.h"
#include "source/common/common/macros.h"
#include "source/common/config/utility.h"
#include "source/common/config/well_known_names.h"
#include "source/common/grpc/common.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/header_utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/socket_impl.h"
#include "source/common/network/utility.h"
#include "source/common/router/router.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/upstream/host_utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Upstream {

// Helper functions to get the correct hostname for an L7 health check.
const std::string& HealthCheckerFactory::getHostname(const HostSharedPtr& host,
                                                     const std::string& config_hostname,
                                                     const ClusterInfoConstSharedPtr& cluster) {
  if (!host->hostnameForHealthChecks().empty()) {
    return host->hostnameForHealthChecks();
  }

  if (!config_hostname.empty()) {
    return config_hostname;
  }

  return cluster->name();
}

class HealthCheckerFactoryContextImpl : public Server::Configuration::HealthCheckerFactoryContext {
public:
  HealthCheckerFactoryContextImpl(Upstream::Cluster& cluster, Envoy::Runtime::Loader& runtime,
                                  Event::Dispatcher& dispatcher,
                                  HealthCheckEventLoggerPtr&& event_logger,
                                  ProtobufMessage::ValidationVisitor& validation_visitor,
                                  Api::Api& api)
      : cluster_(cluster), runtime_(runtime), dispatcher_(dispatcher),
        event_logger_(std::move(event_logger)), validation_visitor_(validation_visitor), api_(api) {
  }
  Upstream::Cluster& cluster() override { return cluster_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_; }
  Event::Dispatcher& mainThreadDispatcher() override { return dispatcher_; }
  HealthCheckEventLoggerPtr eventLogger() override { return std::move(event_logger_); }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Api::Api& api() override { return api_; }

private:
  Upstream::Cluster& cluster_;
  Envoy::Runtime::Loader& runtime_;
  Event::Dispatcher& dispatcher_;
  HealthCheckEventLoggerPtr event_logger_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Api::Api& api_;
};

HealthCheckerSharedPtr HealthCheckerFactory::create(
    const envoy::config::core::v3::HealthCheck& health_check_config, Upstream::Cluster& cluster,
    Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
    AccessLog::AccessLogManager& log_manager,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api) {
  HealthCheckEventLoggerPtr event_logger;
  if (!health_check_config.event_log_path().empty()) {
    event_logger = std::make_unique<HealthCheckEventLoggerImpl>(
        log_manager, dispatcher.timeSource(), health_check_config.event_log_path());
  }
  Server::Configuration::CustomHealthCheckerFactory* factory = nullptr;

  switch (health_check_config.health_checker_case()) {
  case envoy::config::core::v3::HealthCheck::HealthCheckerCase::HEALTH_CHECKER_NOT_SET:
    throw EnvoyException("invalid cluster config");
  case envoy::config::core::v3::HealthCheck::HealthCheckerCase::kHttpHealthCheck:
    factory = &Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::CustomHealthCheckerFactory>("envoy.health_checkers.http");
    break;
  case envoy::config::core::v3::HealthCheck::HealthCheckerCase::kTcpHealthCheck:
    factory = &Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::CustomHealthCheckerFactory>("envoy.health_checkers.tcp");
    break;
  case envoy::config::core::v3::HealthCheck::HealthCheckerCase::kGrpcHealthCheck:
    if (!(cluster.info()->features() & Upstream::ClusterInfo::Features::HTTP2)) {
      throw EnvoyException(fmt::format("{} cluster must support HTTP/2 for gRPC healthchecking",
                                       cluster.info()->name()));
    }
    factory = &Config::Utility::getAndCheckFactoryByName<
        Server::Configuration::CustomHealthCheckerFactory>("envoy.health_checkers.grpc");
    break;
  case envoy::config::core::v3::HealthCheck::HealthCheckerCase::kCustomHealthCheck: {
    factory =
        &Config::Utility::getAndCheckFactory<Server::Configuration::CustomHealthCheckerFactory>(
            health_check_config.custom_health_check());
  }
  }
  std::unique_ptr<Server::Configuration::HealthCheckerFactoryContext> context(
      new HealthCheckerFactoryContextImpl(cluster, runtime, dispatcher, std::move(event_logger),
                                          validation_visitor, api));
  return factory->createCustomHealthChecker(health_check_config, *context);
}

PayloadMatcher::MatchSegments PayloadMatcher::loadProtoBytes(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck::Payload>& byte_array) {
  MatchSegments result;

  for (const auto& entry : byte_array) {
    std::vector<uint8_t> decoded;
    if (entry.has_text()) {
      decoded = Hex::decode(entry.text());
      if (decoded.empty()) {
        throw EnvoyException(fmt::format("invalid hex string '{}'", entry.text()));
      }
    } else {
      decoded.assign(entry.binary().begin(), entry.binary().end());
    }
    if (!decoded.empty()) {
      result.push_back(decoded);
    }
  }

  return result;
}

bool PayloadMatcher::match(const MatchSegments& expected, const Buffer::Instance& buffer) {
  uint64_t start_index = 0;
  for (const std::vector<uint8_t>& segment : expected) {
    ssize_t search_result = buffer.search(segment.data(), segment.size(), start_index);
    if (search_result == -1) {
      return false;
    }

    start_index = search_result + segment.size();
  }

  return true;
}

std::ostream& operator<<(std::ostream& out, HealthState state) {
  switch (state) {
  case HealthState::Unhealthy:
    out << "Unhealthy";
    break;
  case HealthState::Healthy:
    out << "Healthy";
    break;
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, HealthTransition changed_state) {
  switch (changed_state) {
  case HealthTransition::Unchanged:
    out << "Unchanged";
    break;
  case HealthTransition::Changed:
    out << "Changed";
    break;
  case HealthTransition::ChangePending:
    out << "ChangePending";
    break;
  }
  return out;
}

} // namespace Upstream
} // namespace Envoy
