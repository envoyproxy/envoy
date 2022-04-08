#pragma once

#include <chrono>
#include <list>
#include <memory>

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/sip_proxy/filters/network/source/operation.h"
#include "contrib/sip_proxy/filters/network/source/sip.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

#define ALL_PROTOCOL_STATES(FUNCTION)                                                              \
  FUNCTION(StopIteration)                                                                          \
  FUNCTION(WaitForData)                                                                            \
  FUNCTION(TransportBegin)                                                                         \
  FUNCTION(MessageBegin)                                                                           \
  FUNCTION(MessageEnd)                                                                             \
  FUNCTION(TransportEnd)                                                                           \
  FUNCTION(HandleAffinity)                                                                         \
  FUNCTION(Done)

/**
 * ProtocolState represents a set of states used in a state machine to decode
 * Sip requests and responses.
 */
enum class State { ALL_PROTOCOL_STATES(GENERATE_ENUM) };

// Message header list
// HeaderType|TO/FROM/Route/...-------------string //VectorHeader
//           |                   |
//           |                   |----------string
//           |
//           |OTHERS----|HEADER-------------string //VectorPairHeader
//                               |
//                               |----------string
using VectorHeader = std::vector<absl::string_view>;
using VectorPairHeader = std::vector<std::pair<absl::string_view, std::vector<absl::string_view>>>;
using HeaderLine = absl::variant<VectorHeader, VectorPairHeader>;

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

  std::string& rawMsg() { return raw_msg_; }

  MsgType msgType() { return msg_type_; }
  void setMsgType(MsgType data) { msg_type_ = data; }

  MethodType methodType() { return method_type_; }
  void setMethodType(MethodType data) { method_type_ = data; }

  MethodType respMethodType() { return resp_method_type_; }
  void setRespMethodType(MethodType data) { resp_method_type_ = data; }

  absl::optional<absl::string_view> ep() { return ep_; }
  void setEP(absl::string_view data) { ep_ = data; }

  std::vector<Operation>& operationList() { return operation_list_; }
  void setOperation(Operation op) { operation_list_.emplace_back(op); }

  void setPCookieIpMap(std::pair<std::string, std::string>&& data) { p_cookie_ip_map_ = data; }
  absl::optional<std::pair<std::string, std::string>> pCookieIpMap() { return p_cookie_ip_map_; }

  absl::optional<absl::string_view> requestURI() { return request_uri_; }
  void setRequestURI(absl::string_view data) { request_uri_ = data; }

  absl::optional<absl::string_view> topRoute() { return top_route_; }
  void setTopRoute(absl::string_view data) { top_route_ = data; }

  absl::optional<absl::string_view> transactionId() { return transaction_id_; }
  void resetTransactionId() { transaction_id_.reset(); }

  std::string destination() { return destination_; }
  void setDestination(std::string destination) { destination_ = destination; }
  void resetDestination() { destination_.clear(); }

  std::map<std::string, std::string>& paramMap() { return param_map_; };
  void addParam(std::string param, std::string value) { param_map_[param] = value; }
  void resetParam() { param_map_.clear(); }

  std::vector<std::pair<std::string, std::string>>& destinationList() { return destination_list_; }
  void addDestination(std::string param, std::string value) {
    destination_list_.emplace_back(std::make_pair(param, value));
  }
  void resetDestinationList() { destination_list_.clear(); }

  std::map<std::string, bool>& queryMap() { return query_map_; }
  void addQuery(std::string param, bool value) { query_map_[param] = value; }

  std::map<std::string, bool>& subscribeMap() { return subscribe_map_; }
  void addSubscribe(std::string param, bool value) { subscribe_map_[param] = value; }

  bool stopLoadBalance() { return stop_load_balance_; };
  void setStopLoadBalance(bool stop_load_balance) { stop_load_balance_ = stop_load_balance; };

  State state() { return state_; };
  void setState(State state) { state_ = state; };

  std::vector<std::pair<std::string, std::string>>::iterator destIter() { return dest_iter_; };
  void setDestIter(std::vector<std::pair<std::string, std::string>>::iterator dest_iter) {
    dest_iter_ = dest_iter;
  }
  void nextAffinity() { dest_iter_++; };

  void addEPOperation(
      size_t raw_offset, absl::string_view& header,
      const std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>&
          local_services) {
    ENVOY_LOG(trace, "addEPOperation() with header: {}", header);
    if (header.find(";ep=") != absl::string_view::npos) {
      // already Contact have ep
      return;
    }
    auto pos = header.find(">");
    if (pos == absl::string_view::npos) {
      // no url
      return;
    }

    // is domain matched
    if (!isDomainMatched(header, local_services)) {
      ENVOY_LOG(trace, "header {} domain is not equal to local_services domain, don't add EP.",
                header);
      return;
    }

    ENVOY_LOG(trace, "header {} domain is equal to local_services domain, add EP.", header);

    setOperation(Operation(OperationType::Insert, raw_offset + pos, InsertOperationValue(";ep=")));
  }

  void addOpaqueOperation(size_t raw_offset, absl::string_view& header) {
    if (header.find(",opaque=") != absl::string_view::npos) {
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
      auto xsuri = header.find("x-suri=sip:");
      if (xsuri != absl::string_view::npos) {
        setOperation(Operation(OperationType::Delete, raw_offset + xsuri + strlen("x-suri="),
                               DeleteOperationValue(4)));
      }
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

  void addMsgHeader(HeaderType type, absl::string_view value) {
    absl::string_view t, v;

    if (type < HeaderType::TopLine || type > HeaderType::Other) {
      ENVOY_LOG(error, "Wrong HeaderType {}, should be in [{},{}]", type, HeaderType::TopLine,
                HeaderType::Other);
      return;
    }

    if (type == HeaderType::Other) {
      t = value.substr(0, value.find_first_of(": "));
      v = value.substr(value.find_first_of(": ") + strlen(": "),
                       value.length() - t.length() - strlen(": "));
      try {
        auto headerIter = absl::get<VectorPairHeader>(msg_header_list_[type]).begin();
        while (headerIter != absl::get<VectorPairHeader>(msg_header_list_[type]).end()) {
          if (std::string(headerIter->first) == std::string(t)) {
            headerIter->second.emplace_back(v);
            break;
          }
          headerIter++;
        }

        // No vector under this type t
        if (headerIter == absl::get<VectorPairHeader>(msg_header_list_[type]).end()) {
          std::vector<absl::string_view> list{v};
          absl::get<VectorPairHeader>(msg_header_list_[type]).emplace_back(std::make_pair(t, list));
        }
      } catch (absl::bad_variant_access) { // No vector under Other
        std::vector<absl::string_view> list{v};
        msg_header_list_[type] = VectorPairHeader{std::make_pair(t, list)};
      }
      return;
    }
    try {
      absl::get<VectorHeader>(msg_header_list_[type]).emplace_back(value);
    } catch (absl::bad_variant_access) {
      msg_header_list_[type] = VectorHeader{value};
    }
  }
  std::vector<HeaderLine>& msgHeaderList() { return msg_header_list_; }
  std::string getDomainFromHeaderParameter(absl::string_view header, std::string parameter) {
    if (parameter != "host") {
      auto start = header.find(parameter);
      if (start != absl::string_view::npos) {
        // service.parameter() + "="
        start = start + parameter.length() + strlen("=");
        if ("sip:" == header.substr(start, strlen("sip:"))) {
          start += strlen("sip:");
        }
        // end
        auto end = header.find_first_of(":;>", start);
        if (end != absl::string_view::npos) {
          return std::string(header.substr(start, end - start));
        }
      }
    }
    // Parameter is host
    // Or no domain in configured parameter, then try host
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
      return std::string(header.substr(start, end - start));
    } else {
      pos += strlen("@");
      return std::string(addr.substr(pos, addr.length() - pos));
    }
  }

private:
  MsgType msg_type_;
  MethodType method_type_;
  MethodType resp_method_type_;
  std::vector<Operation> operation_list_;
  std::vector<HeaderLine> msg_header_list_{HeaderType::HeaderMaxNum};
  absl::optional<absl::string_view> ep_{};
  absl::optional<absl::string_view> pep_{};
  absl::optional<absl::string_view> route_ep_{};
  absl::optional<absl::string_view> route_opaque_{};

  absl::optional<std::pair<std::string, std::string>> p_cookie_ip_map_{};

  absl::optional<absl::string_view> request_uri_{};
  absl::optional<absl::string_view> top_route_{};
  absl::optional<absl::string_view> transaction_id_{};
  std::string destination_ = "";
  // Params get from Top Route header
  std::map<std::string, std::string> param_map_{};
  // Destination get from param_map_ ordered by CustomizedAffinity, not queried
  std::vector<std::pair<std::string, std::string>> destination_list_{};
  // Could do remote query for this param
  std::map<std::string, bool> query_map_{};
  // Could do remote subscribe for this param
  std::map<std::string, bool> subscribe_map_{};
  // The pointer to handle Affinity
  std::vector<std::pair<std::string, std::string>>::iterator dest_iter_;

  std::string raw_msg_{};
  State state_{State::TransportBegin};
  bool stop_load_balance_{};

  bool isDomainMatched(
      absl::string_view header,
      const std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>&
          local_services) {
    for (auto& service : local_services) {
      if (service.parameter().empty() || service.domain().empty()) {
        // no default value
        continue;
      }
      if (service.domain() == getDomainFromHeaderParameter(header, service.parameter())) {
        return true;
      }
    }
    return false;
  }
};

using MessageMetadataSharedPtr = std::shared_ptr<MessageMetadata>;

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
