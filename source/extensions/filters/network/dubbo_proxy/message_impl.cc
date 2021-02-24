#include "extensions/filters/network/dubbo_proxy/message_impl.h"

#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

RpcInvocationImpl::Attachment::Attachment(MapObjectPtr&& value) : attachment_(std::move(value)) {
  headers_ = Http::RequestHeaderMapImpl::create();

  ASSERT(attachment_);
  ASSERT(attachment_->toMutableUntypedMap());

  for (const auto& pair : *attachment_->toMutableUntypedMap()) {
    const auto key = pair.first->toString();
    const auto value = pair.second->toString();
    if (!key.has_value() || !value.has_value()) {
      continue;
    }
    headers_->addCopy(Http::LowerCaseString(*(key.value())), *(value.value()));
  }
}

void RpcInvocationImpl::Attachment::insert(const std::string& key, const std::string& value) {
  attachment_updated_ = true;

  ASSERT(attachment_->toMutableUntypedMap());

  attachment_->toMutableUntypedMap()->emplace(std::make_unique<Hessian2::StringObject>(key),
                                              std::make_unique<Hessian2::StringObject>(value));

  auto lowcase_key = Http::LowerCaseString(key);
  headers_->remove(lowcase_key);
  headers_->addCopy(lowcase_key, value);
}

void RpcInvocationImpl::Attachment::remove(const std::string& key) {
  attachment_updated_ = true;

  ASSERT(attachment_->toMutableUntypedMap());

  attachment_->toMutableUntypedMap()->erase(std::make_unique<Hessian2::StringObject>(key));
  headers_->remove(Http::LowerCaseString(key));
}

const std::string* RpcInvocationImpl::Attachment::lookup(const std::string& key) {
  ASSERT(attachment_->toMutableUntypedMap());

  auto map = attachment_->toMutableUntypedMap();
  auto result = map->find(std::make_unique<Hessian2::StringObject>(key));
  if (result != map->end() && result->second->toString().has_value()) {
    return result->second->toString().value();
  }
  return nullptr;
}

const RpcInvocationImpl::Attachment& RpcInvocationImpl::attachment() const {
  ASSERT(attachment_);
  return *attachment_;
}

RpcInvocationImpl::AttachmentPtr& RpcInvocationImpl::mutableAttachment() {
  assignAttachmentIfNeed();
  return attachment_;
}

const RpcInvocationImpl::Parameters& RpcInvocationImpl::parameters() const {
  ASSERT(parameters_);
  return *parameters_;
}

RpcInvocationImpl::ParametersPtr& RpcInvocationImpl::mutableParameters() {
  assignParametersIfNeed();
  return parameters_;
}

void RpcInvocationImpl::assignParametersIfNeed() {
  ASSERT(parameters_lazy_callback_);
  if (!parameters_) {
    parameters_lazy_callback_(parameters_);
  }
}

void RpcInvocationImpl::assignAttachmentIfNeed() {
  ASSERT(attachment_lazy_callback_);
  if (attachment_) {
    return;
  }

  assignParametersIfNeed();
  attachment_lazy_callback_(attachment_);

  ASSERT(attachment_);
  if (auto g = attachment_->lookup("group"); g) {
    setServiceGroup(*g);
  }
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
