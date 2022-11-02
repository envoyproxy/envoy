#include "source/extensions/common/dubbo/message_impl.h"

#include <memory>

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

RpcRequestImpl::Attachment::Attachment(MapPtr&& value, size_t offset)
    : attachment_(std::move(value)), attachment_offset_(offset) {
  ASSERT(attachment_ != nullptr);
  ASSERT(attachment_->toMutableUntypedMap().has_value());
}

void RpcRequestImpl::Attachment::insert(absl::string_view key, absl::string_view value) {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  attachment_updated_ = true;

  Hessian2::ObjectPtr key_o = std::make_unique<String>(key);
  Hessian2::ObjectPtr val_o = std::make_unique<String>(value);
  attachment_->toMutableUntypedMap().value().get().insert_or_assign(std::move(key_o),
                                                                    std::move(val_o));
}

void RpcRequestImpl::Attachment::remove(absl::string_view key) {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  attachment_updated_ = true;
  attachment_->toMutableUntypedMap().value().get().erase(key);
}

absl::optional<absl::string_view> RpcRequestImpl::Attachment::lookup(absl::string_view key) const {
  ASSERT(attachment_->toMutableUntypedMap().has_value());

  auto& map = attachment_->toMutableUntypedMap().value().get();
  auto result = map.find(key);
  if (result != map.end() && result->second->type() == Hessian2::Object::Type::String) {
    ASSERT(result->second->toString().has_value());
    return absl::make_optional<absl::string_view>(result->second->toString().value().get());
  }
  return absl::nullopt;
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

  if (auto g = attachment_->lookup("group"); g.has_value()) {
    const_cast<RpcRequestImpl*>(this)->group_ = std::string(g.value());
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
