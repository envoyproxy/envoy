#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/extensions/health_check/event_sinks/file/v3/file.pb.h"
#include "envoy/extensions/health_check/event_sinks/file/v3/file.pb.validate.h"
#include "envoy/upstream/health_check_event_sink.h"

namespace Envoy {
namespace Upstream {

class HealthCheckEventFileSink : public HealthCheckEventSink {
public:
  explicit HealthCheckEventFileSink(
      const envoy::extensions::health_check::event_sinks::file::v3::HealthCheckEventFileSink&
          config,
      AccessLog::AccessLogManager& log_manager) {
    auto file_or_error = log_manager.createAccessLog(
        Filesystem::FilePathAndType{Filesystem::DestinationType::File, config.event_log_path()});
    THROW_IF_NOT_OK_REF(file_or_error.status());
    file_ = file_or_error.value();
  }

  void log(envoy::data::core::v3::HealthCheckEvent event) override;

private:
  AccessLog::AccessLogFileSharedPtr file_;
};

class HealthCheckEventFileSinkFactory : public HealthCheckEventSinkFactory {
public:
  HealthCheckEventFileSinkFactory() = default;

  HealthCheckEventSinkPtr
  createHealthCheckEventSink(const ProtobufWkt::Any& config,
                             Server::Configuration::HealthCheckerFactoryContext& context) override;

  std::string name() const override { return "envoy.health_check.event_sink.file"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::health_check::event_sinks::file::v3::HealthCheckEventFileSink()};
  }
};

} // namespace Upstream
} // namespace Envoy
