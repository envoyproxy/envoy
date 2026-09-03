#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/network/socket.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"
#include "envoy/upstream/health_checker.h"

#include "source/common/common/dump_state_utils.h"
#include "source/common/common/logger.h"
#include "source/common/grpc/codec.h"
#include "source/common/http/codec_client.h"
#include "source/common/router/header_parser.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/upstream/health_checker_event_logger.h"

#include "src/proto/grpc/health/v1/health.pb.h"

namespace Envoy {
namespace Upstream {

constexpr uint64_t kDefaultMaxBytesInBuffer = 1024;

/**
 * HealthCheckerHash and HealthCheckerEqualTo are used to allow the HealthCheck proto to be used as
 * a flat_hash_map key.
 */
struct HealthCheckerHash {
  size_t operator()(const envoy::config::core::v3::HealthCheck& health_check) const {
    return MessageUtil::hash(health_check);
  }
};

struct HealthCheckerEqualTo {
  bool operator()(const envoy::config::core::v3::HealthCheck& lhs,
                  const envoy::config::core::v3::HealthCheck& rhs) const {
    return Protobuf::util::MessageDifferencer::Equals(lhs, rhs);
  }
};

/**
 * Health checker factory context.
 */
class HealthCheckerFactoryContextImpl : public Server::Configuration::HealthCheckerFactoryContext {
public:
  HealthCheckerFactoryContextImpl(Upstream::Cluster& cluster,
                                  Server::Configuration::ServerFactoryContext& server_context,
                                  Stats::Scope& stats_scope,
                                  HealthFlagCallbacks health_flag_callbacks = {})
      : cluster_(cluster), runtime_(server_context.runtime()),
        dispatcher_(server_context.mainThreadDispatcher()),
        validation_visitor_(server_context.messageValidationVisitor()),
        log_manager_(server_context.accessLogManager()), api_(server_context.api()),
        server_context_(server_context), stats_scope_(stats_scope),
        health_flag_callbacks_(std::move(health_flag_callbacks)) {}
  Upstream::Cluster& cluster() override { return cluster_; }
  Envoy::Runtime::Loader& runtime() override { return runtime_; }
  Event::Dispatcher& mainThreadDispatcher() override { return dispatcher_; }
  HealthCheckEventLoggerPtr eventLogger() override { return std::move(event_logger_); }
  ProtobufMessage::ValidationVisitor& messageValidationVisitor() override {
    return validation_visitor_;
  }
  Api::Api& api() override { return api_; }

  AccessLog::AccessLogManager& accessLogManager() override { return log_manager_; }
  void setEventLogger(HealthCheckEventLoggerPtr event_logger) override {
    event_logger_ = std::move(event_logger);
  }

  Server::Configuration::ServerFactoryContext& serverFactoryContext() override {
    return server_context_;
  };

  Stats::Scope& statsScope() override { return stats_scope_; }
  HealthFlagCallbacks healthFlagCallbacks() const override { return health_flag_callbacks_; }

private:
  Upstream::Cluster& cluster_;
  Envoy::Runtime::Loader& runtime_;
  Event::Dispatcher& dispatcher_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  AccessLog::AccessLogManager& log_manager_;
  Api::Api& api_;
  HealthCheckEventLoggerPtr event_logger_;
  Server::Configuration::ServerFactoryContext& server_context_;
  Stats::Scope& stats_scope_;
  HealthFlagCallbacks health_flag_callbacks_;
};

/**
 * Factory for creating health checker implementations.
 */
class HealthCheckerFactory : public Logger::Loggable<Logger::Id::health_checker> {
public:
  // Helper functions to get the correct hostname for an L7 health check.
  static const std::string& getHostname(const HostSharedPtr& host,
                                        const std::string& config_hostname,
                                        const ClusterInfoConstSharedPtr& cluster);
  /**
   * Create a health checker or return an error.
   * @param health_check_config supplies the health check proto.
   * @param cluster supplies the owning cluster.
   * @param server_context reference to the Server context object
   * @param stats_scope optional scope override for health check stats.
   * @param health_flag_callbacks optional overrides for health flag operations.
   * @return a health checker.
   */
  static absl::StatusOr<HealthCheckerSharedPtr>
  create(const envoy::config::core::v3::HealthCheck& health_check_config,
         Upstream::Cluster& cluster, Server::Configuration::ServerFactoryContext& server_context,
         OptRef<Stats::Scope> stats_scope = std::nullopt,
         HealthFlagCallbacks health_flag_callbacks = {});
};

/**
 * Utility class for loading a binary health checking config and matching it against a buffer.
 * Split out for ease of testing. The type of matching performed is the following (this is the
 * MongoDB health check request and response):
 *
 * "send": [
    {"text": "39000000"},
    {"text": "EEEEEEEE"},
    {"text": "00000000"},
    {"text": "d4070000"},
    {"text": "00000000"},
    {"text": "746573742e"},
    {"text": "24636d6400"},
    {"text": "00000000"},
    {"text": "FFFFFFFF"},

    {"text": "13000000"},
    {"text": "01"},
    {"text": "70696e6700"},
    {"text": "000000000000f03f"},
    {"text": "00"}
   ],
   "receive": [
    {"text": "EEEEEEEE"},
    {"text": "01000000"},
    {"text": "00000000"},
    {"text": "0000000000000000"},
    {"text": "00000000"},
    {"text": "11000000"},
    {"text": "01"},
    {"text": "6f6b"},
    {"text": "00000000000000f03f"},
    {"text": "00"}
   ]
 * Each text or binary filed in Payload is converted to a binary block.
 * The text is Hex string by default.
 *
 * During each health check cycle, all of the "send" bytes are sent to the target server. Each
 * binary block can be of arbitrary length and is just concatenated together when sent.
 *
 * On the receive side, "fuzzy" matching is performed such that each binary block must be found,
 * and in the order specified, but not necessary contiguous. Thus, in the example above,
 * "FFFFFFFF" could be inserted in the response between "EEEEEEEE" and "01000000" and the check
 * would still pass.
 *
 */
class PayloadMatcher {
public:
  using MatchSegments = std::list<std::vector<uint8_t>>;

  static absl::StatusOr<MatchSegments> loadProtoBytes(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck::Payload>& byte_array);
  static absl::StatusOr<MatchSegments>
  loadProtoBytes(const envoy::config::core::v3::HealthCheck::Payload& single_payload);
  static bool match(const MatchSegments& expected, const Buffer::Instance& buffer);

private:
  static absl::StatusOr<std::vector<uint8_t>>
  decodePayload(const envoy::config::core::v3::HealthCheck::Payload& payload);
};

} // namespace Upstream
} // namespace Envoy
