#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/http/header_map_impl.h"

#include "extensions/filters/network/dubbo_proxy/message.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

class MessageMetadata {
public:
  // TODO(gengleilei) Add parameter data types and implement Dubbo data type mapping.
  using ParameterValueMap = std::unordered_map<uint32_t, std::string>;
  using ParameterValueMapPtr = std::unique_ptr<ParameterValueMap>;

  using HeaderMapPtr = std::unique_ptr<Http::HeaderMapImpl>;

  void setServiceName(const std::string& name) { service_name_ = name; }
  const std::string& service_name() const { return service_name_; }

  void setMethodName(const std::string& name) { method_name_ = name; }
  const absl::optional<std::string>& method_name() const { return method_name_; }

  void setServiceVersion(const std::string& version) { service_version_ = version; }
  const absl::optional<std::string>& service_version() const { return service_version_; }

  void setServiceGroup(const std::string& group) { group_ = group; }
  const absl::optional<std::string>& service_group() const { return group_; }

  void setMessageType(MessageType type) { message_type_ = type; }
  MessageType message_type() const { return message_type_; }

  void setRequestId(int64_t id) { request_id_ = id; }
  int64_t request_id() const { return request_id_; }

  void setSerializationType(SerializationType type) { serialization_type_ = type; }
  SerializationType serialization_type() const { return serialization_type_; }

  void setTwoWayFlag(bool two_way) { is_two_way_ = two_way; }
  bool is_two_way() const { return is_two_way_; }

  void setEventFlag(bool is_event) { is_event_ = is_event; }
  bool is_event() const { return is_event_; }

  void setResponseStatus(ResponseStatus status) { response_status_ = status; }
  const absl::optional<ResponseStatus>& response_status() const { return response_status_; }

  void addParameterValue(uint32_t index, const std::string& value) {
    assignParameterIfNeed();
    parameter_map_->emplace(index, value);
  }
  const std::string& getParameterValue(uint32_t index) const {
    if (parameter_map_) {
      auto itor = parameter_map_->find(index);
      if (itor != parameter_map_->end()) {
        return itor->second;
      }
    }

    return EMPTY_STRING;
  }
  bool hasParameters() const { return parameter_map_ != nullptr; }
  const ParameterValueMap& parameters() {
    ASSERT(hasParameters());
    return *parameter_map_;
  }

  bool hasHeaders() const { return headers_ != nullptr; }
  const Http::HeaderMap& headers() const {
    ASSERT(hasHeaders());
    return *headers_;
  }
  void addHeader(const std::string& key, const std::string& value) {
    assignHeaderIfNeed();
    headers_->addCopy(Http::LowerCaseString(key), value);
  }
  void addHeaderReference(const Http::LowerCaseString& key, const std::string& value) {
    assignHeaderIfNeed();
    headers_->addReference(key, value);
  }

private:
  inline void assignHeaderIfNeed() {
    if (!headers_) {
      headers_ = std::make_unique<Http::HeaderMapImpl>();
    }
  }
  inline void assignParameterIfNeed() {
    if (!parameter_map_) {
      parameter_map_ = std::make_unique<ParameterValueMap>();
    }
  }

  bool is_two_way_{false};
  bool is_event_{false};

  MessageType message_type_{MessageType::Request};
  SerializationType serialization_type_{SerializationType::Hessian};
  absl::optional<ResponseStatus> response_status_;

  int64_t request_id_ = 0;

  // Routing metadata.
  std::string service_name_;
  absl::optional<std::string> method_name_;
  absl::optional<std::string> service_version_;
  absl::optional<std::string> group_;
  ParameterValueMapPtr parameter_map_;
  HeaderMapPtr headers_; // attachment
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
