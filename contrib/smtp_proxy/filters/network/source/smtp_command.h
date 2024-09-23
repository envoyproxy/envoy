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

  enum class ResponseType { None, Local, ViaUpstream };

  SmtpCommand(const std::string& name, SmtpCommand::Type type, TimeSource& time_source)
      : name_(name), type_(type), time_source_(time_source),
        start_time_(time_source.monotonicTime()) {}

  SmtpCommand(const std::string& name, int resp_code, const std::string msg, std::string details, TimeSource& time_source)
      : name_(name), response_code_(resp_code), response_(msg), response_code_details_(details), time_source_(time_source) {}

  SmtpCommand::Type getType() { return type_; }
  std::string& getName() { return name_; }

  int& getResponseCode() { return response_code_; }
  void setResponseCodeDetails(std::string& resp_code_details) {
    response_code_details_ = resp_code_details;
  }
  std::string& getResponseCodeDetails() { return response_code_details_; }
  std::string& getResponseMsg() { return response_; }
  bool isLocalResponseSet() { return response_type_ == SmtpCommand::ResponseType::Local; }
  int64_t& getDuration() { return duration_; }

  void onComplete(std::string& response, const std::string& resp_code_details, int response_code,
                  SmtpCommand::ResponseType resp_type) {

    if (!local_resp_generated_) {
      response_type_ = resp_type;
      response_code_ = response_code;
      response_ = response;
      response_code_details_ = resp_code_details;
    }

    auto end_time_ = time_source_.monotonicTime();
    const auto response_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time_ - start_time_);
    duration_ = response_time.count();
  }

  void storeLocalResponse(std::string response, const std::string& resp_code_details,
                          int response_code) {
    local_resp_generated_ = true;
    response_type_ = ResponseType::Local;
    response_code_ = response_code;
    response_ = response;
    response_code_details_ = resp_code_details;
  }

private:
  std::string name_;
  int response_code_{0};
  std::string response_;
  std::string response_code_details_;
  SmtpCommand::ResponseType response_type_{SmtpCommand::ResponseType::None};
  SmtpCommand::Type type_{SmtpCommand::Type::None};
  TimeSource& time_source_;
  const MonotonicTime start_time_;
  int64_t duration_ = 0;
  bool local_resp_generated_ = false;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
