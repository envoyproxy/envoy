#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session_filters/http_capsule/v3/http_capsule.pb.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/udp/udp_proxy/session_filters/filter.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {

class HttpCapsuleFilter : public Filter, Logger::Loggable<Logger::Id::http> {
public:
  // ReadFilter
  ReadFilterStatus onNewSession() override;
  ReadFilterStatus onData(Network::UdpRecvData& data) override;
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // WriteFilter
  WriteFilterStatus onWrite(Network::UdpRecvData& data) override;
  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

private:
  ReadFilterCallbacks* read_callbacks_{};
  WriteFilterCallbacks* write_callbacks_{};
};

} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
