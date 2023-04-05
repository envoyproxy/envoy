#pragma once

#include <cstdint>

#include "envoy/access_log/access_log.h"
#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/core/v3/health_check.pb.h"
#include "envoy/data/core/v3/health_check_event.pb.h"
#include "envoy/grpc/status.h"
#include "envoy/network/socket.h"
#include "envoy/server/health_checker_config.h"
#include "envoy/type/v3/http.pb.h"
#include "envoy/type/v3/range.pb.h"

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
 * Factory for creating health checker implementations.
 */
class HealthCheckerFactory : public Logger::Loggable<Logger::Id::health_checker> {
public:
  // Helper functions to get the correct hostname for an L7 health check.
  static const std::string& getHostname(const HostSharedPtr& host,
                                        const std::string& config_hostname,
                                        const ClusterInfoConstSharedPtr& cluster);
  /**
   * Create a health checker.
   * @param health_check_config supplies the health check proto.
   * @param cluster supplies the owning cluster.
   * @param runtime supplies the runtime loader.
   * @param dispatcher supplies the dispatcher.
   * @param log_manager supplies the log_manager.
   * @param validation_visitor message validation visitor instance.
   * @param api reference to the Api object
   * @return a health checker.
   */
  static HealthCheckerSharedPtr
  create(const envoy::config::core::v3::HealthCheck& health_check_config,
         Upstream::Cluster& cluster, Runtime::Loader& runtime, Event::Dispatcher& dispatcher,
         AccessLog::AccessLogManager& log_manager,
         ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);
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

  static MatchSegments loadProtoBytes(
      const Protobuf::RepeatedPtrField<envoy::config::core::v3::HealthCheck::Payload>& byte_array);
  static bool match(const MatchSegments& expected, const Buffer::Instance& buffer);
};

} // namespace Upstream
} // namespace Envoy
