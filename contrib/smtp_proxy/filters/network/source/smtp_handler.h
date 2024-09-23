#pragma once
#include <cstdint>

#include "envoy/common/platform.h"
#include "envoy/network/connection.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpHandler {
public:
  virtual ~SmtpHandler() = default;

  virtual bool isTerminated() PURE;
  virtual bool isDataTransferInProgress() PURE;
  virtual bool isCommandInProgress() PURE;
  virtual SmtpUtils::Result handleCommand(std::string& command, std::string& args) PURE;
  virtual SmtpUtils::Result handleResponse(int& response_code, std::string& response) PURE;

  virtual void updateBytesMeterOnCommand(Buffer::Instance& data) PURE;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
