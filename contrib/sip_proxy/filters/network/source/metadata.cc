#include "contrib/sip_proxy/filters/network/source/metadata.h"

#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

std::vector<std::string> methodStr{"INVITE", "REGISTER", "UPDATE", "REFER", "SUBSCRIBE", "NOTIFY",
                                   "ACK",    "BYE",      "CANCEL", "OK",    "FAILURE",   "NULL"};

void SipHeader::parseHeader() {
  if (!params_.empty()) {
    // Already parsed
    return;
  }

  std::size_t pos = 0;
  std::string pattern = "(.*)=(.*?)>*";
  absl::string_view& header = raw_text_;

  // Has "SIP/2.0" in top line
  // Eg: INVITE sip:User.0000@tas01.defult.svc.cluster.local SIP/2.0
  if (std::size_t found = header.find(" SIP"); found != absl::string_view::npos) {
    header = header.substr(0, found);
  }

  while (std::size_t found = header.find_first_of(";>", pos)) {
    absl::string_view str;
    if (found == absl::string_view::npos) {
      str = header.substr(pos);
    } else {
      str = header.substr(pos, found - pos);
    }

    std::string param = "";
    std::string value = "";
    re2::RE2::FullMatch(std::string(str), pattern, &param, &value);

    if (!param.empty() && !value.empty()) {
      if (value.find("sip:") != absl::string_view::npos) {
        value = value.substr(std::strlen("sip:"));
      }

      if (!value.empty()) {
        std::size_t comma = value.find(':');
        if (comma != absl::string_view::npos) {
          value = value.substr(0, comma);
        }
      }

      if (!value.empty()) {
        if (param == "opaque") {
          auto value_view = header.substr(header.find(value), value.length());
          params_.emplace_back(std::make_pair("ep", value_view));
        } else {
          auto param_view = header.substr(header.find(param), param.length());
          auto value_view = header.substr(header.find(value), value.length());
          params_.emplace_back(std::make_pair(param_view, value_view));
        }
      }
    }

    if (found == absl::string_view::npos) {
      break;
    }
    pos = found + 1;
  }
}

// input is the full SIP header
void MessageMetadata::setTransactionId(absl::string_view data) {
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

void MessageMetadata::addEPOperation(
    size_t raw_offset, absl::string_view& header,
    const std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>&
        local_services) {
  if (header.find(";ep=") != absl::string_view::npos) {
    // already have ep
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

void MessageMetadata::addOpaqueOperation(size_t raw_offset, absl::string_view& header) {
  if (header.find(",opaque=") != absl::string_view::npos) {
    // already has opaque
    return;
  }
  auto pos = header.length();
  setOperation(
      Operation(OperationType::Insert, raw_offset + pos, InsertOperationValue(",opaque=")));
}

void MessageMetadata::deleteInstipOperation(size_t raw_offset, absl::string_view& header) {
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

void MessageMetadata::addMsgHeader(HeaderType type, absl::string_view value) {
  switch (type) {
  case HeaderType::TopLine:
    headers_[type].emplace_back(SipHeader(type, value));
    break;
  case HeaderType::Other:
    // TODO
    break;
  default:
    if (type <= HeaderType::TopLine || type >= HeaderType::Other) {
      ENVOY_LOG(error, "Wrong HeaderType {}, should be in [{},{}]", type, HeaderType::TopLine,
                HeaderType::Other);
      return;
    }
    headers_[type].emplace_back(SipHeader(type, value));
  }
}

std::string MessageMetadata::getDomainFromHeaderParameter(absl::string_view& header,
                                                          const std::string& parameter) {
  // Parameter default is host
  if (!parameter.empty() && parameter != "host") {
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
  // TopLine should be absl::string_view::npos
  if (end == absl::string_view::npos) {
    end = header.length();
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

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
