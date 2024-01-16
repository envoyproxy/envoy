#include "contrib/generic_proxy/filters/network/source/codecs/dubbo/config.h"

#include <memory>

#include "envoy/registry/registry.h"

#include "source/extensions/common/dubbo/message_impl.h"

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
  const auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_metadata_->mutableRequest());
  ASSERT(typed_request != nullptr);

  for (const auto& pair : typed_request->attachment().attachment()) {
    ASSERT(pair.first != nullptr && pair.second != nullptr);

    if (pair.first->type() == Hessian2::Object::Type::String &&
        pair.second->type() == Hessian2::Object::Type::String) {
      ASSERT(pair.first->toString().has_value() && pair.second->toString().has_value());

      if (!callback(pair.first->toString().value().get(), pair.second->toString().value().get())) {
        break;
      }
    }
  }
}

absl::optional<absl::string_view> DubboRequest::get(absl::string_view key) const {
  if (key == VERSION_KEY) {
    return inner_metadata_->request().serviceVersion();
  }
  const auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_metadata_->mutableRequest());
  ASSERT(typed_request != nullptr);

  return typed_request->attachment().lookup(key);
}

void DubboRequest::set(absl::string_view key, absl::string_view val) {
  auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_metadata_->mutableRequest());
  ASSERT(typed_request != nullptr);

  typed_request->mutableAttachment()->insert(key, val);
}

void DubboRequest::erase(absl::string_view key) {
  auto* typed_request =
      dynamic_cast<Common::Dubbo::RpcRequestImpl*>(&inner_metadata_->mutableRequest());
  ASSERT(typed_request != nullptr);

  typed_request->mutableAttachment()->remove(key);
}

void DubboResponse::refreshStatus() {
  ASSERT(inner_metadata_ != nullptr);
  ASSERT(inner_metadata_->hasResponse() && inner_metadata_->hasResponseStatus());

  using Common::Dubbo::RpcResponseType;

  const auto status = inner_metadata_->context().responseStatus();
  const auto optional_type = inner_metadata_->response().responseType();

  // The final status is not ok if the response status is not ResponseStatus::Ok
  // anyway.
  bool response_ok = (status == Common::Dubbo::ResponseStatus::Ok);

  // The final status is not ok if the response type is ResponseWithException or
  // ResponseWithExceptionWithAttachments even if the response status is Ok.
  if (status == Common::Dubbo::ResponseStatus::Ok) {
    ASSERT(optional_type.has_value());
    auto type = optional_type.value_or(RpcResponseType::ResponseWithException);
    if (type == RpcResponseType::ResponseWithException ||
        type == RpcResponseType::ResponseWithExceptionWithAttachments) {
      response_ok = false;
    }
  }

  status_ = StreamStatus(static_cast<uint8_t>(status), response_ok);
}

DubboCodecBase::DubboCodecBase(Common::Dubbo::DubboCodecPtr codec) : codec_(std::move(codec)) {}

ResponsePtr DubboServerCodec::respond(Status status, absl::string_view,
                                      const Request& origin_request) {
  const auto* typed_request = dynamic_cast<const DubboRequest*>(&origin_request);
  ASSERT(typed_request != nullptr);

  Common::Dubbo::ResponseStatus response_status = genericStatusToStatus(status.code());

  absl::optional<Common::Dubbo::RpcResponseType> optional_type;
  absl::string_view content;

  if (response_status == Common::Dubbo::ResponseStatus::Ok) {
    optional_type.emplace(Common::Dubbo::RpcResponseType::ResponseWithException);
    content = "exception_via_proxy";
  } else {
    content = status.message();
  }

  return std::make_unique<DubboResponse>(Common::Dubbo::DirectResponseUtil::localResponse(
      *typed_request->inner_metadata_, response_status, optional_type, content));
}

CodecFactoryPtr
DubboCodecFactoryConfig::createCodecFactory(const Protobuf::Message&,
                                            Envoy::Server::Configuration::FactoryContext&) {
  return std::make_unique<DubboCodecFactory>();
}

REGISTER_FACTORY(DubboCodecFactoryConfig, CodecFactoryConfig);

} // namespace Dubbo
} // namespace Codec
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
