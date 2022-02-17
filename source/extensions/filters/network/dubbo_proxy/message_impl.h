#pragma once

#include "envoy/http/header_map.h"

#include "source/extensions/filters/network/dubbo_proxy/hessian_utils.h"
#include "source/extensions/filters/network/dubbo_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class ContextImpl : public Context {
public:
  // DubboProxy::Context
  size_t headerSize() const override { return header_size_; }
  size_t bodySize() const override { return body_size_; }
  bool isHeartbeat() const override { return is_heartbeat_; }

  void setHeaderSize(size_t size) { header_size_ = size; }
  void setBodySize(size_t size) { body_size_ = size; }
  void setHeartbeat(bool heartbeat) { is_heartbeat_ = heartbeat; }

private:
  size_t header_size_{0};
  size_t body_size_{0};

  bool is_heartbeat_{false};
};

class RpcInvocationBase : public RpcInvocation {
public:
  ~RpcInvocationBase() override = default;

  void setServiceName(const std::string& name) { service_name_ = name; }
  const std::string& serviceName() const override { return service_name_; }

  void setMethodName(const std::string& name) { method_name_ = name; }
  const std::string& methodName() const override { return method_name_; }

  void setServiceVersion(const std::string& version) { service_version_ = version; }
  const absl::optional<std::string>& serviceVersion() const override { return service_version_; }

  void setServiceGroup(const std::string& group) { group_ = group; }
  const absl::optional<std::string>& serviceGroup() const override { return group_; }

protected:
  std::string service_name_;
  std::string method_name_;
  absl::optional<std::string> service_version_;
  absl::optional<std::string> group_;
};

class RpcInvocationImpl : public RpcInvocationBase {
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

    void insert(const std::string& key, const std::string& value);
    void remove(const std::string& key);
    const std::string* lookup(const std::string& key) const;

    // Http::HeaderMap wrapper to attachment.
    const Http::HeaderMap& headers() const { return *headers_; }

    // Whether the attachment should be re-serialized.
    bool attachmentUpdated() const { return attachment_updated_; }

    size_t attachmentOffset() const { return attachment_offset_; }

  private:
    bool attachment_updated_{false};

    MapPtr attachment_;

    // The binary offset of attachment in the original message. Retaining this value can help
    // subsequent re-serialization of the attachment without re-serializing the parameters.
    size_t attachment_offset_{};

    // To reuse the HeaderMatcher API and related tools provided by Envoy, we store the key/value
    // pair of the string type in the attachment in the Http::HeaderMap. This introduces additional
    // overhead and ignores the case of the key in the attachment. But for now, it's acceptable.
    Http::HeaderMapPtr headers_;
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

  const absl::optional<std::string>& serviceGroup() const override;

private:
  void assignParametersIfNeed() const;
  void assignAttachmentIfNeed() const;

  AttachmentLazyCallback attachment_lazy_callback_;
  ParametersLazyCallback parameters_lazy_callback_;

  mutable ParametersPtr parameters_{};
  mutable AttachmentPtr attachment_{};
};

class RpcResultImpl : public RpcResult {
public:
  bool hasException() const override { return has_exception_; }
  void setException(bool has_exception) { has_exception_ = has_exception; }

private:
  bool has_exception_{false};
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
