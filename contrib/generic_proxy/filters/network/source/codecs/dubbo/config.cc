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
constexpr absl::string_view UNKNOWN_RESPONSE_STATUS = "UnknownResponseStatus";

#define ENUM_TO_STRING_VIEW(X)                                                                     \
  case Common::Dubbo::ResponseStatus::X:                                                           \
    static constexpr absl::string_view X##_VIEW = #X;                                              \
    return X##_VIEW;

absl::string_view responseStatusToStringView(Common::Dubbo::ResponseStatus status) {
  switch (status) {
    ENUM_TO_STRING_VIEW(Ok);
    ENUM_TO_STRING_VIEW(ClientTimeout);
    ENUM_TO_STRING_VIEW(ServerTimeout);
    ENUM_TO_STRING_VIEW(BadRequest);
    ENUM_TO_STRING_VIEW(BadResponse);
    ENUM_TO_STRING_VIEW(ServiceNotFound);
    ENUM_TO_STRING_VIEW(ServiceError);
    ENUM_TO_STRING_VIEW(ServerError);
    ENUM_TO_STRING_VIEW(ClientError);
    ENUM_TO_STRING_VIEW(ServerThreadpoolExhaustedError);
  }
  return UNKNOWN_RESPONSE_STATUS;
}

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

StatusCode statusToGenericStatus(Common::Dubbo::ResponseStatus status) {
  switch (status) {
  case Common::Dubbo::ResponseStatus::Ok:
    return StatusCode::kOk;
  case Common::Dubbo::ResponseStatus::ClientTimeout:
  case Common::Dubbo::ResponseStatus::ServerTimeout:
    return StatusCode::kUnknown;
  case Common::Dubbo::ResponseStatus::BadRequest:
    return StatusCode::kInvalidArgument;
  case Common::Dubbo::ResponseStatus::BadResponse:
    return StatusCode::kUnknown;
  case Common::Dubbo::ResponseStatus::ServiceNotFound:
    return StatusCode::kNotFound;
  case Common::Dubbo::ResponseStatus::ServiceError:
  case Common::Dubbo::ResponseStatus::ServerError:
  case Common::Dubbo::ResponseStatus::ClientError:
    return StatusCode::kUnavailable;
  case Common::Dubbo::ResponseStatus::ServerThreadpoolExhaustedError:
    return StatusCode::kResourceExhausted;
  }
  return StatusCode::kUnavailable;
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

void DubboResponse::refreshGenericStatus() {
  ASSERT(inner_metadata_ != nullptr);
  ASSERT(inner_metadata_->hasResponse() && inner_metadata_->hasResponseStatus());

  using Common::Dubbo::RpcResponseType;

  const auto status = inner_metadata_->context().responseStatus();
  const auto optional_type = inner_metadata_->response().responseType();

  if (status == Common::Dubbo::ResponseStatus::Ok) {
    ASSERT(optional_type.has_value());
    auto type = optional_type.value_or(RpcResponseType::ResponseWithException);
    if (type == RpcResponseType::ResponseWithException ||
        type == RpcResponseType::ResponseWithExceptionWithAttachments) {
      status_ = Status(StatusCode::kUnavailable, "exception_via_upstream");
      return;
    }
    status_ = absl::OkStatus();
    return;
  }

  status_ = Status(statusToGenericStatus(status), responseStatusToStringView(status));
}

DubboCodecBase::DubboCodecBase(Common::Dubbo::DubboCodecPtr codec) : codec_(std::move(codec)) {}

ResponsePtr DubboServerCodec::respond(Status status, absl::string_view,
                                      const Request& origin_request) {
  const auto* typed_request = dynamic_cast<const DubboRequest*>(&origin_request);
  ASSERT(typed_request != nullptr);

  Common::Dubbo::ResponseStatus response_status;
  absl::optional<Common::Dubbo::RpcResponseType> optional_type;
  absl::string_view content;
  if (status.ok()) {
    response_status = Common::Dubbo::ResponseStatus::Ok;
    optional_type.emplace(Common::Dubbo::RpcResponseType::ResponseWithException);
    content = "exception_via_proxy";
  } else {
    response_status = genericStatusToStatus(status.code());
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
