#pragma once

#include "envoy/access_log/access_log_config.h"

#include "source/common/protobuf/protobuf.h"

#include "test/integration/fake_access_log.pb.h"

namespace Envoy {

using LogSignature =
    std::function<void(const Formatter::HttpFormatterContext&, const StreamInfo::StreamInfo&)>;

class FakeAccessLog : public AccessLog::Instance {
public:
  FakeAccessLog(LogSignature cb) : log_cb_(cb) {}

  void log(const Formatter::HttpFormatterContext& context,
           const StreamInfo::StreamInfo& info) override {
    if (log_cb_) {
      log_cb_(context, info);
    }
  }

private:
  LogSignature log_cb_;
};

class FakeAccessLogFactory : public AccessLog::AccessLogInstanceFactory {
public:
  AccessLog::InstanceSharedPtr
  createAccessLogInstance(const Protobuf::Message&, AccessLog::FilterPtr&&,
                          Server::Configuration::FactoryContext&) override {
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
