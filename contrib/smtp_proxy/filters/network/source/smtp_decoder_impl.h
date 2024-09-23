#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

#include "absl/container/flat_hash_map.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// SMTP message decoder.
class Decoder {
public:
  struct Command {
    std::string verb;
    std::string args;
    size_t len;
  };

  struct Response {
    int resp_code;
    std::string msg;
    size_t len;
  };

  virtual ~Decoder() = default;

  // virtual SmtpUtils::Result onData(Buffer::Instance& data, bool) PURE;
  virtual SmtpUtils::Result parseCommand(Buffer::Instance& data, Command& command) PURE;
  virtual SmtpUtils::Result parseResponse(Buffer::Instance& data, Response& response) PURE;
  // virtual SmtpSession* getSession() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl() = default;

  ~DecoderImpl() {}
  // void setSession(SmtpSession* session) { session_ = session; }
  // SmtpUtils::Result onData(Buffer::Instance& data, bool upstream) override;
  // SmtpSession* getSession() override { return session_; }
  SmtpUtils::Result parseCommand(Buffer::Instance& data, Command& command) override;
  SmtpUtils::Result parseResponse(Buffer::Instance& data, Response& response) override;
  SmtpUtils::Result getLine(Buffer::Instance& data, size_t max_line, std::string& line_out);
  SmtpUtils::Result isValidSmtpLine(Buffer::Instance& data, size_t max_len, std::string& output);
  // protected:
  // DecoderCallbacks* callbacks_{};
  // SmtpSession* session_;
  // Buffer::OwnedImpl response_;
  // Buffer::OwnedImpl last_response_;
  // Buffer::OwnedImpl response_on_hold_;

  // TimeSource& time_source_;
  // Random::RandomGenerator& random_generator_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
