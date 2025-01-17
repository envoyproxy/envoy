#include "source/extensions/filters/network/dubbo_proxy/message_impl.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

RpcInvocationImpl::Attachment::Attachment(MapPtr&& value, size_t offset)
    : attachment_(std::move(value)), attachment_offset_(offset) {
  headers_ = Http::RequestHeaderMapImpl::create();

  ASSERT(attachment_ != nullptr);
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  for (const auto& pair : *attachment_) {
    const auto key = pair.first->toString();
    const auto value = pair.second->toString();
    if (!key.has_value() || !value.has_value()) {
      continue;
    }
    headers_->addCopy(Http::LowerCaseString(key.value().get()), value.value().get());
  }
}

void RpcInvocationImpl::Attachment::insert(const std::string& key, const std::string& value) {
  attachment_updated_ = true;

  attachment_->emplace(std::make_unique<String>(key), std::make_unique<String>(value));

  auto lowcase_key = Http::LowerCaseString(key);
  headers_->remove(lowcase_key);
  headers_->addCopy(lowcase_key, value);
}

void RpcInvocationImpl::Attachment::remove(const std::string& key) {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  attachment_updated_ = true;
  attachment_->toMutableUntypedMap().value().get().erase(key);
  headers_->remove(Http::LowerCaseString(key));
}

const std::string* RpcInvocationImpl::Attachment::lookup(const std::string& key) const {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  auto& map = attachment_->toMutableUntypedMap().value().get();
  auto result = map.find(key);
  if (result != map.end() && result->second->toString().has_value()) {
    return &(result->second->toString().value().get());
  }
  return nullptr;
}

void RpcInvocationImpl::assignParametersIfNeed() const {
  ASSERT(parameters_lazy_callback_ != nullptr);
  if (parameters_ == nullptr) {
    parameters_ = parameters_lazy_callback_();
  }
}

void RpcInvocationImpl::assignAttachmentIfNeed() const {
  ASSERT(attachment_lazy_callback_ != nullptr);
  if (attachment_ != nullptr) {
    return;
  }

  assignParametersIfNeed();
  attachment_ = attachment_lazy_callback_();

  if (auto g = attachment_->lookup("group"); g != nullptr) {
    const_cast<RpcInvocationImpl*>(this)->group_ = *g;
  }
}

const absl::optional<std::string>& RpcInvocationImpl::serviceGroup() const {
  assignAttachmentIfNeed();
  return group_;
}

const RpcInvocationImpl::Attachment& RpcInvocationImpl::attachment() const {
  assignAttachmentIfNeed();
  return *attachment_;
}

RpcInvocationImpl::AttachmentPtr& RpcInvocationImpl::mutableAttachment() const {
  assignAttachmentIfNeed();
  return attachment_;
}

const RpcInvocationImpl::Parameters& RpcInvocationImpl::parameters() const {
  assignParametersIfNeed();
  return *parameters_;
}

RpcInvocationImpl::ParametersPtr& RpcInvocationImpl::mutableParameters() const {
  assignParametersIfNeed();
  return parameters_;
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
