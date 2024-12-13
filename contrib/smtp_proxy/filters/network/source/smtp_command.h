#pragma once
#include <cstdint>
#include <string>

#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

class SmtpCommand {
public:
  enum class Type {
    None = 0,
    NonTransactionCommand,
    TransactionCommand,
    Others,
  };

  SmtpCommand(const std::string& name, SmtpCommand::Type type, TimeSource& time_source)
      : name_(name), type_(type), time_source_(time_source),
        start_time_(time_source.monotonicTime()) {}

  SmtpCommand::Type getType() { return type_; }
  std::string& getName() { return name_; }

  uint16_t& getResponseCode() { return response_code_; }
  int64_t& getDuration() { return duration_; }

  void onComplete(std::string& response, uint16_t response_code) {
    response_code_ = response_code;
    response_ = response;
    auto end_time_ = time_source_.monotonicTime();
    const auto response_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time_ - start_time_);
    duration_ = response_time.count();
  }

private:
  std::string name_;
  uint16_t response_code_{0};
  std::string response_;
  SmtpCommand::Type type_{SmtpCommand::Type::None};
  TimeSource& time_source_;
  const MonotonicTime start_time_;
  int64_t duration_ = 0;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
