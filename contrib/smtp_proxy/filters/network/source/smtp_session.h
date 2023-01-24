#pragma once
#include <cstdint>

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Class stores data about the current state of a transaction between smtp client and server.
class SmtpSession {
public:
  bool inTransaction() { return in_transaction_; };
  void setInTransaction(bool in_transaction) { in_transaction_ = in_transaction; };

private:
  bool in_transaction_{false};
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
