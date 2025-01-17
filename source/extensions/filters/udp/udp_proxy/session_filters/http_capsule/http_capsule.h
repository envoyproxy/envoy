#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/session/http_capsule/v3/http_capsule.pb.h"
#include "envoy/network/filter.h"
#include "envoy/network/listener.h"

#include "source/common/common/logger.h"

#include "quiche/common/capsule.h"
#include "quiche/common/simple_buffer_allocator.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {

using Filter = Network::UdpSessionFilter;
using ReadFilterStatus = Network::UdpSessionReadFilterStatus;
using WriteFilterStatus = Network::UdpSessionWriteFilterStatus;
using ReadFilterCallbacks = Network::UdpSessionReadFilterCallbacks;
using WriteFilterCallbacks = Network::UdpSessionWriteFilterCallbacks;

class HttpCapsuleFilter : public Filter,
                          public quiche::CapsuleParser::Visitor,
                          Logger::Loggable<Logger::Id::http> {
public:
  HttpCapsuleFilter(TimeSource& time_source) : time_source_(time_source) {}

  // ReadFilter
  ReadFilterStatus onNewSession() override { return ReadFilterStatus::Continue; }
  ReadFilterStatus onData(Network::UdpRecvData& data) override;
  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    read_callbacks_ = &callbacks;
  }

  // WriteFilter
  WriteFilterStatus onWrite(Network::UdpRecvData& data) override;
  void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) override {
    write_callbacks_ = &callbacks;
  }

  // quiche::CapsuleParser::Visitor
  bool OnCapsule(const quiche::Capsule& capsule) override;
  void OnCapsuleParseFailure(absl::string_view error_message) override;

private:
  ReadFilterCallbacks* read_callbacks_{};
  WriteFilterCallbacks* write_callbacks_{};
  quiche::CapsuleParser capsule_parser_{this};
  quiche::SimpleBufferAllocator capsule_buffer_allocator_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::Address::InstanceConstSharedPtr peer_address_;
  TimeSource& time_source_;
};

} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
