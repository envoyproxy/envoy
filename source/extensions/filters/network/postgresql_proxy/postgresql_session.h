#pragma once
#include <cstdint>

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

class PostgreSQLSession : Logger::Loggable<Logger::Id::filter> {
public:
  enum class ProtocolDirection {
    Frontend,
    Backend,
    Both
  };

  enum class ProtocolType {
    Simple,
    Extended
  };

  void setProtocolDirection(PostgreSQLSession::ProtocolDirection protocol_direction) { protocol_direction_ = protocol_direction; }
  PostgreSQLSession::ProtocolDirection getProtocolDirection() { return protocol_direction_; }

  void setProtocolType(PostgreSQLSession::ProtocolType protocol_type) { protocol_type_ = protocol_type; }
  PostgreSQLSession::ProtocolType getProtocolType() { return protocol_type_; }

  bool inTransaction() { return in_transaction_; };
  void setInTransaction(bool in_transaction) { in_transaction_ = in_transaction; };

private:
  PostgreSQLSession::ProtocolDirection protocol_direction_;
  PostgreSQLSession::ProtocolType protocol_type_;
  bool in_transaction_{false};
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
