#pragma once

#include <chrono>
#include <iostream>
#include <list>
#include <memory>

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "contrib/sip_proxy/filters/network/source/operation.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"

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
class MessageMetadata : public Logger::Loggable<Logger::Id::filter> {
public:
  MessageMetadata() = default;
  MessageMetadata(std::string&& raw_msg) : raw_msg_(std::move(raw_msg)) {}

  MsgType msgType() { return msg_type_; }
  MethodType methodType() { return method_type_; }
  MethodType respMethodType() { return resp_method_type_; }
  absl::optional<absl::string_view> ep() { return ep_; }
  std::vector<Operation>& operationList() { return operation_list_; }
  absl::optional<absl::string_view> routeEP() { return route_ep_; }
  absl::optional<absl::string_view> routeOpaque() { return route_opaque_; }

  absl::optional<absl::string_view> requestURI() { return request_uri_; }
  absl::optional<absl::string_view> topRoute() { return top_route_; }
  absl::optional<absl::string_view> domain() { return domain_; }
  absl::optional<absl::string_view> transactionId() { return transaction_id_; }
  absl::optional<absl::string_view> destination() { return destination_; }

  std::string& rawMsg() { return raw_msg_; }

  void setMsgType(MsgType data) { msg_type_ = data; }
  void setMethodType(MethodType data) { method_type_ = data; }
  void setRespMethodType(MethodType data) { resp_method_type_ = data; }
  void setOperation(Operation op) { operation_list_.emplace_back(op); }
  void setEP(absl::string_view data) { ep_ = data; }
  void setRouteEP(absl::string_view data) { route_ep_ = data; }
  void setRouteOpaque(absl::string_view data) { route_opaque_ = data; }

  void setRequestURI(absl::string_view data) { request_uri_ = data; }
  void setTopRoute(absl::string_view data) { top_route_ = data; }
  void setDomain(absl::string_view header, std::string domain_match_param_name) {
    domain_ = getDomain(header, domain_match_param_name);
  }

  // void addEPOperation(size_t raw_offset, absl::string_view& header, std::string& own_domain,
  // std::string& domain_match_param_name) {
  void addEPOperation(size_t raw_offset, absl::string_view& header, std::string own_domain,
                      std::string domain_match_param_name) {
    ENVOY_LOG(debug, "header: {}\n own_domain: {}\n  domain_match_param_name: {}", header,
              own_domain, domain_match_param_name);
    if (header.find(";ep=") != absl::string_view::npos) {
      // already Contact have ep
      return;
    }
    auto pos = header.find(">");
    if (pos == absl::string_view::npos) {
      // no url
      return;
    }

    // Get domain
    absl::string_view domain = getDomain(header, domain_match_param_name);

    // Compare the domain
    if (domain != own_domain) {
      ENVOY_LOG(debug, "header domain:{} not matches own_domain:{}", domain, own_domain);
      return;
    }

    setOperation(Operation(OperationType::Insert, raw_offset + pos, InsertOperationValue(";ep=")));
  }

  void addOpaqueOperation(size_t raw_offset, absl::string_view& header) {
    if (header.find("opaque=") != absl::string_view::npos) {
      // already has opaque
      return;
    }
    auto pos = header.length();
    setOperation(
        Operation(OperationType::Insert, raw_offset + pos, InsertOperationValue(",opaque=")));
  }

  void deleteInstipOperation(size_t raw_offset, absl::string_view& header) {
    // Delete inst-ip and remove "sip:" in x-suri
    if (auto pos = header.find(";inst-ip="); pos != absl::string_view::npos) {
      setOperation(
          Operation(OperationType::Delete, raw_offset + pos,
                    DeleteOperationValue(
                        header.substr(pos, header.find_first_of(";>", pos + 1) - pos).size())));
      auto xsuri = header.find("sip:pcsf-cfed");
      setOperation(Operation(OperationType::Delete, raw_offset + xsuri, DeleteOperationValue(4)));
    }
  }

  // input is the full SIP header
  void setTransactionId(absl::string_view data) {
    auto start_index = data.find("branch=");
    if (start_index == absl::string_view::npos) {
      return;
    }
    start_index += strlen("branch=");

    auto end_index = data.find_first_of(";>", start_index);
    if (end_index == absl::string_view::npos) {
      end_index = data.size();
    }
    transaction_id_ = data.substr(start_index, end_index - start_index);
  }

  void setDestination(absl::string_view destination) { destination_ = destination; }
  /*only used in UT*/
  void resetTransactionId() { transaction_id_.reset(); }

private:
  MsgType msg_type_;
  MethodType method_type_;
  MethodType resp_method_type_;
  std::vector<Operation> operation_list_;
  absl::optional<absl::string_view> ep_{};
  absl::optional<absl::string_view> pep_{};
  absl::optional<absl::string_view> route_ep_{};
  absl::optional<absl::string_view> route_opaque_{};

  absl::optional<absl::string_view> request_uri_{};
  absl::optional<absl::string_view> top_route_{};
  absl::optional<absl::string_view> domain_{};
  absl::optional<absl::string_view> transaction_id_{};
  absl::optional<absl::string_view> destination_{};

  std::string raw_msg_{};

  absl::string_view getDomain(absl::string_view header, std::string domain_match_param_name) {
    ENVOY_LOG(debug, "header: {}\ndomain_match_param_name: {}", header, domain_match_param_name);

    // Get domain
    absl::string_view domain = "";

    if (domain_match_param_name != "host") {
      auto start = header.find(domain_match_param_name);
      if (start == absl::string_view::npos) {
        domain = "";
      } else {
        // domain_match_param_name + "="
        // start = start + strlen(domain_match_param_name.c_str()) + strlen("=") ;
        start = start + domain_match_param_name.length() + strlen("=");
        if ("sip:" == header.substr(start, strlen("sip:"))) {
          start += strlen("sip:");
        }
        // end
        auto end = header.find_first_of(":;>", start);
        if (end == absl::string_view::npos) {
          domain = "";
        } else {
          domain = header.substr(start, end - start);
        }
      }
    }

    // Still get host if mapped domain is empty
    if (domain_match_param_name == "host" || domain == "") {
      auto start = header.find("sip:");
      if (start == absl::string_view::npos) {
        return "";
      }
      start += strlen("sip:");
      auto end = header.find_first_of(":;>", start);
      if (end == absl::string_view::npos) {
        return "";
      }

      auto addr = header.substr(start, end - start);

      // Remove name in format of sip:name@addr:pos
      auto pos = addr.find("@");
      if (pos == absl::string_view::npos) {
        domain = header.substr(start, end - start);
      } else {
        pos += strlen("@");
        domain = addr.substr(pos, addr.length() - pos);
      }
    }

    return domain;
  }
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
