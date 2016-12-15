#pragma once

#include "envoy/http/access_log.h"

namespace Http {
namespace AccessLog {

struct RequestInfoImpl : public RequestInfo {
  RequestInfoImpl(Protocol protocol)
      : protocol_(protocol), start_time_(std::chrono::system_clock::now()) {}

  // Http::AccessLog::RequestInfo
  SystemTime startTime() const override { return start_time_; }

  uint64_t bytesReceived() const override { return bytes_received_; }

  Protocol protocol() const override { return protocol_; }
  void protocol(Protocol protocol) override { protocol_ = protocol; }

  const Optional<uint32_t>& responseCode() const override { return response_code_; }

  uint64_t bytesSent() const override { return bytes_sent_; }

  std::chrono::milliseconds duration() const override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() -
                                                                 start_time_);
  }

  void setResponseFlag(Http::AccessLog::ResponseFlag response_flag) override {
    response_flags_ |= response_flag;
  }

  bool getResponseFlag(ResponseFlag flag) const override { return response_flags_ & flag; }

  void onUpstreamHostSelected(Upstream::HostDescriptionPtr host) override { upstream_host_ = host; }

  Upstream::HostDescriptionPtr upstreamHost() const override { return upstream_host_; }

  bool healthCheck() const override { return hc_request_; }

  void healthCheck(bool is_hc) override { hc_request_ = is_hc; }

  Protocol protocol_;
  const SystemTime start_time_;
  uint64_t bytes_received_{};
  Optional<uint32_t> response_code_;
  uint64_t bytes_sent_{};
  uint64_t response_flags_{};
  Upstream::HostDescriptionPtr upstream_host_{};
  bool hc_request_{};
};

} // AccessLog
} // Http
