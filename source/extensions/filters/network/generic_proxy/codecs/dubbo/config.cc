#include "source/extensions/filters/network/generic_proxy/codecs/dubbo/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/extensions/common/dubbo/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Codec {
namespace Dubbo {

namespace {

constexpr absl::string_view VERSION_KEY = "version";

Common::Dubbo::ResponseStatus genericStatusToStatus(StatusCode code) {
  switch (code) {
  case StatusCode::kOk:
    return Common::Dubbo::ResponseStatus::Ok;
  case StatusCode::kInvalidArgument:
    return Common::Dubbo::ResponseStatus::BadRequest;
  default:
    return Common::Dubbo::ResponseStatus::ServerError;
  }
}

} // namespace

void DubboRequest::forEach(IterateCallback callback) const {
  for (const auto& [key, val] : inner_metadata_->request().content().attachments()) {
    if (!callback(key, val)) {
      break;
    }
  }
}

absl::optional<absl::string_view> DubboRequest::get(absl::string_view key) const {
  if (key == VERSION_KEY) {
    return inner_metadata_->request().serviceVersion();
  }

  auto it = inner_metadata_->request().content().attachments().find(key);
  if (it == inner_metadata_->request().content().attachments().end()) {
    return absl::nullopt;
  }

  return absl::string_view{it->second};
}

void DubboRequest::set(absl::string_view key, absl::string_view val) {
  inner_metadata_->request().content().setAttachment(key, val);
}

void DubboRequest::erase(absl::string_view key) {
  inner_metadata_->request().content().delAttachment(key);
}

void DubboResponse::refreshStatus() {
  ASSERT(inner_metadata_ != nullptr);
  ASSERT(inner_metadata_->hasResponse() && inner_metadata_->hasResponseStatus());

  using Common::Dubbo::RpcResponseType;

  const auto status = inner_metadata_->context().responseStatus();
  const auto optional_type = inner_metadata_->response().responseType();

  if (status != Common::Dubbo::ResponseStatus::Ok) {
    status_ = StreamStatus(static_cast<uint8_t>(status), false);
    return;
  }

  bool response_ok = true;
  // The final status is not ok if the response type is ResponseWithException or
  // ResponseWithExceptionWithAttachments even if the response status is Ok.
  ASSERT(optional_type.has_value());
  auto type = optional_type.value_or(RpcResponseType::ResponseWithException);
  if (type == RpcResponseType::ResponseWithException ||
      type == RpcResponseType::ResponseWithExceptionWithAttachments) {
    response_ok = false;
  }

  status_ = StreamStatus(static_cast<uint8_t>(status), response_ok);
}

DubboCodecBase::DubboCodecBase(Common::Dubbo::DubboCodecPtr codec) : codec_(std::move(codec)) {}

ResponsePtr DubboServerCodec::respond(Status status, absl::string_view data,
                                      const Request& origin_request) {
  const auto* typed_request = dynamic_cast<const DubboRequest*>(&origin_request);
  ASSERT(typed_request != nullptr);

  Common::Dubbo::ResponseStatus response_status = genericStatusToStatus(status.code());

  absl::optional<Common::Dubbo::RpcResponseType> optional_type;

  if (response_status == Common::Dubbo::ResponseStatus::Ok) {
    optional_type.emplace(Common::Dubbo::RpcResponseType::ResponseWithException);
  }
  auto response = Common::Dubbo::DirectResponseUtil::localResponse(
      *typed_request->inner_metadata_, response_status, optional_type, data);

  if (!status.ok()) {
    response->mutableResponse().content().setAttachment("reason", status.message());
  } else {
    response->mutableResponse().content().setAttachment("reason", "envoy_response");
  }

  return std::make_unique<DubboResponse>(std::move(response));
}

CodecFactoryPtr
DubboCodecFactoryConfig::createCodecFactory(const Protobuf::Message&,
                                            Envoy::Server::Configuration::ServerFactoryContext&) {
  return std::make_unique<DubboCodecFactory>();
}

REGISTER_FACTORY(DubboCodecFactoryConfig, CodecFactoryConfig);

} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
