#pragma once

#include <chrono>
#include <memory>

#include "absl/types/optional.h"
#include "absl/strings/string_view.h"
#include "extensions/filters/network/sip_proxy/sip.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

/**
 * MessageMetadata encapsulates metadata about Sip messages. The various fields are considered
 * optional since they may come from either the transport or protocol in some cases. Unless
 * otherwise noted, accessor methods throw absl::bad_optional_access if the corresponding value has
 * not been set.
 */
class MessageMetadata {
public:
  MessageMetadata(){};
  MessageMetadata(std::string&& raw_msg)
      : raw_msg_(std::move(raw_msg)), timestamp_(std::chrono::system_clock::now()) {}

  std::chrono::system_clock::time_point timestamp() { return timestamp_; };

  MsgType msgType() { return msg_type_; }
  MethodType methodType() { return method_type_; }
  MethodType respMethodType() { return resp_method_type_; }
  absl::optional<size_t> insertEPLocation() { return insert_ep_location_; }
  absl::optional<size_t> insertTagLocation() { return insert_tag_location_; }
  absl::optional<absl::string_view> EP() { return ep_; }

  absl::optional<absl::string_view> requestURI() { return request_uri_; }
  absl::optional<absl::string_view> topRoute() { return top_route_; }
  absl::optional<absl::string_view> domain() { return domain_; }
  absl::optional<absl::string_view> transactionId() { return transaction_id_; }

  std::string& rawMsg() { return raw_msg_; }

  void setMsgType(MsgType data) { msg_type_ = data; }
  void setMethodType(MethodType data) { method_type_ = data; }
  void setRespMethodType(MethodType data) { resp_method_type_ = data; }
  void setInsertEPLocation(size_t data) { insert_ep_location_ = data; }
  void setInsertTagLocation(size_t data) { insert_tag_location_ = data; }
  void setEP(absl::string_view data) { ep_ = data; }

  void setRequestURI(absl::string_view data) { request_uri_ = data; }
  void setTopRoute(absl::string_view data) { top_route_ = data; }
  void setDomain(absl::string_view data) { domain_ = data; }
  void setTransactionId(absl::string_view data) { transaction_id_ = data; }

private:
  MsgType msg_type_;
  MethodType method_type_;
  MethodType resp_method_type_;
  absl::optional<size_t> insert_ep_location_{};
  absl::optional<size_t> insert_tag_location_{};
  absl::optional<absl::string_view> ep_{};
  absl::optional<absl::string_view> pep_{};

  absl::optional<absl::string_view> request_uri_{};
  absl::optional<absl::string_view> top_route_{};
  absl::optional<absl::string_view> domain_{};
  absl::optional<absl::string_view> transaction_id_{};

  std::string raw_msg_{};
  std::chrono::system_clock::time_point timestamp_;
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
