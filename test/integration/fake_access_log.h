#pragma once

#include "envoy/server/access_log_config.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/fake_access_log.pb.h"

namespace Envoy {

using LogSignature = std::function<void(
    const Http::RequestHeaderMap*, const Http::ResponseHeaderMap*, const Http::ResponseTrailerMap*,
    const StreamInfo::StreamInfo&, AccessLog::AccessLogType)>;

class FakeAccessLog : public AccessLog::Instance {
public:
  FakeAccessLog(LogSignature cb) : log_cb_(cb) {}

  void log(const Http::RequestHeaderMap* request_headers,
           const Http::ResponseHeaderMap* response_headers,
           const Http::ResponseTrailerMap* response_trailers,
           const StreamInfo::StreamInfo& stream_info,
           AccessLog::AccessLogType access_log_type) override {
    if (log_cb_) {
      log_cb_(request_headers, response_headers, response_trailers, stream_info, access_log_type);
    }
  }

private:
  LogSignature log_cb_;
};

class FakeAccessLogFactory : public Server::Configuration::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message&, AccessLog::FilterPtr&&,
                          Server::Configuration::ListenerAccessLogFactoryContext&) override {
    std::lock_guard<std::mutex> guard(log_callback_lock_);
    auto access_log_instance = std::make_shared<FakeAccessLog>(log_cb_);
    access_log_instances_.push_back(access_log_instance);
    return access_log_instance;
  }

  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message&, AccessLog::FilterPtr&&,
                          Server::Configuration::CommonFactoryContext&) override {
    std::lock_guard<std::mutex> guard(log_callback_lock_);
    auto access_log_instance = std::make_shared<FakeAccessLog>(log_cb_);
    access_log_instances_.push_back(access_log_instance);
    return access_log_instance;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new test::integration::accesslog::FakeAccessLog()};
  }

  void setLogCallback(LogSignature callable) {
    std::lock_guard<std::mutex> guard(log_callback_lock_);
    log_cb_ = callable;
  }

  std::string name() const override { return "envoy.access_loggers.test"; }

private:
  std::mutex log_callback_lock_;
  LogSignature log_cb_{nullptr};
  std::vector<AccessLog::InstanceSharedPtr> access_log_instances_;
};

} // namespace Envoy
