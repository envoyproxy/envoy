#pragma once
#include <cstdint>

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgreSQLProxy {

// Class stores data about the current state of a transaction between postgresql client and server.
class PostgreSQLSession {
public:
  bool inTransaction() { return in_transaction_; };
  void setInTransaction(bool in_transaction) { in_transaction_ = in_transaction; };

private:
  bool in_transaction_{false};
};

} // namespace PostgreSQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
