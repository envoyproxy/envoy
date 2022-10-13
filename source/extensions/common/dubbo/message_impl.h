#pragma once

#include <string>

#include "source/extensions/common/dubbo/hessian2_utils.h"
#include "source/extensions/common/dubbo/message.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

class RpcRequestBase : public RpcRequest {
public:
  void setServiceName(absl::string_view name) { service_name_ = std::string(name); }
  void setMethodName(absl::string_view name) { method_name_ = std::string(name); }
  void setServiceVersion(absl::string_view version) { service_version_ = std::string(version); }
  void setServiceGroup(absl::string_view group) { group_ = std::string(group); }

  // RpcRequest
  absl::string_view serviceName() const override { return service_name_; }
  absl::string_view methodName() const override { return method_name_; }
  absl::string_view serviceVersion() const override { return service_version_; }
  absl::optional<absl::string_view> serviceGroup() const override {
    return group_.has_value() ? absl::make_optional<absl::string_view>(group_.value())
                              : absl::nullopt;
  }

protected:
  std::string service_name_;
  std::string method_name_;
  std::string service_version_;
  absl::optional<std::string> group_;
};

class RpcRequestImpl : public RpcRequestBase {
public:
  // Each parameter consists of a parameter binary size and Hessian2::Object.
  using Parameters = std::vector<Hessian2::ObjectPtr>;
  using ParametersPtr = std::unique_ptr<Parameters>;

  class Attachment {
  public:
    using Map = Hessian2::UntypedMapObject;
    using MapPtr = std::unique_ptr<Hessian2::UntypedMapObject>;
    using String = Hessian2::StringObject;

    Attachment(MapPtr&& value, size_t offset);

    const Map& attachment() const { return *attachment_; }

    void insert(absl::string_view key, absl::string_view value);
    void remove(absl::string_view key);
    absl::optional<absl::string_view> lookup(absl::string_view key) const;

    // Whether the attachment should be re-serialized.
    bool attachmentUpdated() const { return attachment_updated_; }

    size_t attachmentOffset() const { return attachment_offset_; }

  private:
    bool attachment_updated_{false};

    MapPtr attachment_;

    // The binary offset of attachment in the original message. Retaining this value can help
    // subsequent re-serialization of the attachment without re-serializing the parameters.
    size_t attachment_offset_{};
  };
  using AttachmentPtr = std::unique_ptr<Attachment>;

  using AttachmentLazyCallback = std::function<AttachmentPtr()>;
  using ParametersLazyCallback = std::function<ParametersPtr()>;

  bool hasParameters() const { return parameters_ != nullptr; }
  const Parameters& parameters() const;
  ParametersPtr& mutableParameters() const;

  bool hasAttachment() const { return attachment_ != nullptr; }
  const Attachment& attachment() const;
  AttachmentPtr& mutableAttachment() const;

  void setParametersLazyCallback(ParametersLazyCallback&& callback) {
    parameters_lazy_callback_ = std::move(callback);
  }

  void setAttachmentLazyCallback(AttachmentLazyCallback&& callback) {
    attachment_lazy_callback_ = std::move(callback);
  }

  absl::optional<absl::string_view> serviceGroup() const override;

  Buffer::Instance& messageBuffer() { return message_buffer_; }

private:
  void assignParametersIfNeed() const;
  void assignAttachmentIfNeed() const;

  // Original request message from downstream.
  Buffer::OwnedImpl message_buffer_;

  AttachmentLazyCallback attachment_lazy_callback_;
  ParametersLazyCallback parameters_lazy_callback_;

  mutable ParametersPtr parameters_{};
  mutable AttachmentPtr attachment_{};
};

class RpcResponseImpl : public RpcResponse {
public:
  void setResponseType(RpcResponseType type) { response_type_ = type; }

  // RpcResponse
  absl::optional<RpcResponseType> responseType() const override { return response_type_; }

  Buffer::Instance& messageBuffer() { return message_buffer_; }

  void setLocalRawMessage(absl::string_view val) { local_raw_message_ = std::string(val); }
  absl::optional<absl::string_view> localRawMessage() {
    return local_raw_message_.has_value()
               ? absl::make_optional<absl::string_view>(local_raw_message_.value())
               : absl::nullopt;
  }

private:
  // Original response message from upstream.
  Buffer::OwnedImpl message_buffer_;

  // Optional raw content for local direct response.
  absl::optional<std::string> local_raw_message_;

  absl::optional<RpcResponseType> response_type_;
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
