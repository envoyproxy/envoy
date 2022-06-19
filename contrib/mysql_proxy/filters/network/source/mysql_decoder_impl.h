#pragma once
#include "contrib/mysql_proxy/filters/network/source/mysql_decoder.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MySQLProxy {

class DecoderImpl : public Decoder, public Logger::Loggable<Logger::Id::filter> {
public:
  DecoderImpl(DecoderCallbacks& callbacks) : callbacks_(callbacks) {}

  // MySQLProxy::Decoder
  void onData(Buffer::Instance& data, bool is_upstream) override;
  MySQLSession& getSession() override { return session_; }

private:
  bool decode(Buffer::Instance& data, bool is_upstream);
  void parseMessage(Buffer::Instance& message, uint8_t seq, uint32_t len, bool is_upstream);

  DecoderCallbacks& callbacks_;
  MySQLSession session_;
};

class DecoderFactoryImpl : public DecoderFactory {
public:
  DecoderPtr create(DecoderCallbacks& callbacks) override;
  static DecoderFactoryImpl instance_;
};

} // namespace MySQLProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
