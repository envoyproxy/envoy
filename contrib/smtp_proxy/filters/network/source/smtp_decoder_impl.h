#pragma once
#include <cstdint>

#include "contrib/smtp_proxy/filters/network/source/smtp_decoder.h"
#include "contrib/smtp_proxy/filters/network/source/smtp_session.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// SMTP message decoder.
class Decoder {
public:
  virtual ~Decoder() = default;

  virtual SmtpUtils::Result onData(Buffer::Instance& data, bool) PURE;
  virtual SmtpUtils::Result parseCommand(Buffer::Instance& data) PURE;
  virtual SmtpUtils::Result parseResponse(Buffer::Instance& data) PURE;
  virtual SmtpSession* getSession() PURE;
};

using DecoderPtr = std::unique_ptr<Decoder>;

class DecoderImpl : public Decoder, Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks* callbacks, TimeSource& time_source,
              Random::RandomGenerator& random_generator);

  ~DecoderImpl() {
    delete session_;
    session_ = nullptr;
  }
  void setSession(SmtpSession* session) { session_ = session; }
  SmtpUtils::Result onData(Buffer::Instance& data, bool upstream) override;
  SmtpSession* getSession() override { return session_; }
  SmtpUtils::Result parseCommand(Buffer::Instance& data) override;
  SmtpUtils::Result parseResponse(Buffer::Instance& data) override;

protected:
  SmtpSession* session_;
  DecoderCallbacks* callbacks_{};
  Buffer::OwnedImpl response_;
  Buffer::OwnedImpl last_response_;
  Buffer::OwnedImpl response_on_hold_;

  TimeSource& time_source_;
  Random::RandomGenerator& random_generator_;
};

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy