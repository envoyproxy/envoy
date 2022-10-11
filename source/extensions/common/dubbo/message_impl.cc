#include "source/extensions/common/dubbo/message_impl.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

RpcRequestImpl::Attachment::Attachment(MapPtr&& value, size_t offset)
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

void RpcRequestImpl::Attachment::insert(const std::string& key, const std::string& value) {
  attachment_updated_ = true;

  attachment_->emplace(std::make_unique<String>(key), std::make_unique<String>(value));

  auto lowcase_key = Http::LowerCaseString(key);
  headers_->remove(lowcase_key);
  headers_->addCopy(lowcase_key, value);
}

void RpcRequestImpl::Attachment::remove(const std::string& key) {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  attachment_updated_ = true;
  attachment_->toMutableUntypedMap().value().get().erase(key);
  headers_->remove(Http::LowerCaseString(key));
}

const std::string* RpcRequestImpl::Attachment::lookup(const std::string& key) const {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  auto& map = attachment_->toMutableUntypedMap().value().get();
  auto result = map.find(key);
  if (result != map.end() && result->second->toString().has_value()) {
    return &(result->second->toString().value().get());
  }
  return nullptr;
}

void RpcRequestImpl::assignParametersIfNeed() const {
  ASSERT(parameters_lazy_callback_ != nullptr);
  if (parameters_ == nullptr) {
    parameters_ = parameters_lazy_callback_();
  }
}

void RpcRequestImpl::assignAttachmentIfNeed() const {
  ASSERT(attachment_lazy_callback_ != nullptr);
  if (attachment_ != nullptr) {
    return;
  }

  assignParametersIfNeed();
  attachment_ = attachment_lazy_callback_();

  if (auto g = attachment_->lookup("group"); g != nullptr) {
    const_cast<RpcRequestImpl*>(this)->group_ = *g;
  }
}

absl::optional<absl::string_view> RpcRequestImpl::serviceGroup() const {
  assignAttachmentIfNeed();
  return RpcRequestBase::serviceGroup();
}

const RpcRequestImpl::Attachment& RpcRequestImpl::attachment() const {
  assignAttachmentIfNeed();
  return *attachment_;
}

RpcRequestImpl::AttachmentPtr& RpcRequestImpl::mutableAttachment() const {
  assignAttachmentIfNeed();
  return attachment_;
}

const RpcRequestImpl::Parameters& RpcRequestImpl::parameters() const {
  assignParametersIfNeed();
  return *parameters_;
}

RpcRequestImpl::ParametersPtr& RpcRequestImpl::mutableParameters() const {
  assignParametersIfNeed();
  return parameters_;
}

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
