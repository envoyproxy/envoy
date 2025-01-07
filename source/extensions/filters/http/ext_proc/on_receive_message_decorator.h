#pragma once

#include <memory>
#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/service/ext_proc/v3/external_processor.pb.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

class OnReceiveMessageDecorator {
public:
  virtual ~OnReceiveMessageDecorator() = default;
  virtual void onReceiveMessage(const envoy::service::ext_proc::v3::ProcessingResponse& response,
                                absl::Status processing_status,
                                Envoy::StreamInfo::StreamInfo&) PURE;
};

class OnReceiveMessageDecoratorFactory : public Config::TypedFactory {
public:
  ~OnReceiveMessageDecoratorFactory() override = default;
  virtual std::unique_ptr<OnReceiveMessageDecorator>
  createDecorator(const Protobuf::Message& config,
                  const Envoy::Server::Configuration::CommonFactoryContext& context) const PURE;

  std::string category() const override {
    return "envoy.http.ext_proc.on_receive_message_decorator";
  }
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
