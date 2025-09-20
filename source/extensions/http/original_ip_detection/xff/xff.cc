#include "source/extensions/http/original_ip_detection/xff/xff.h"

#include "source/common/http/utility.h"
#include "source/common/network/cidr_range.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

absl::StatusOr<std::unique_ptr<XffIPDetection>> XffIPDetection::create(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config) {

  if (config.has_xff_trusted_cidrs() && config.xff_num_trusted_hops() > 0) {
    return absl::InvalidArgumentError("Cannot set both xff_num_trusted_hops and xff_trusted_cidrs");
  }
  return std::unique_ptr<XffIPDetection>(new XffIPDetection(config));
}

XffIPDetection::XffIPDetection(
    const envoy::extensions::http::original_ip_detection::xff::v3::XffConfig& config)
    : xff_num_trusted_hops_(config.xff_num_trusted_hops()),
      skip_xff_append_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, skip_xff_append, true)),
      use_remote_address_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, use_remote_address, false)),
      append_x_forwarded_port_(config.append_x_forwarded_port()) {
  if (config.has_xff_trusted_cidrs()) {
    xff_trusted_cidrs_.reserve(config.xff_trusted_cidrs().cidrs().size());
    for (const envoy::config::core::v3::CidrRange& entry : config.xff_trusted_cidrs().cidrs()) {
      absl::StatusOr<Network::Address::CidrRange> cidr_or_error =
          Network::Address::CidrRange::create(entry);
      if (cidr_or_error.ok()) {
        xff_trusted_cidrs_.push_back(cidr_or_error.value());
      }
    }
  }
}

XffIPDetection::XffIPDetection(uint32_t xff_num_trusted_hops, bool skip_xff_append)
    : xff_num_trusted_hops_(xff_num_trusted_hops), skip_xff_append_(skip_xff_append),
      use_remote_address_(false), append_x_forwarded_port_(false) {}

XffIPDetection::XffIPDetection(const std::vector<Network::Address::CidrRange> trusted_cidrs,
                               bool skip_xff_append)
    : xff_num_trusted_hops_(0), xff_trusted_cidrs_(trusted_cidrs),
      skip_xff_append_(skip_xff_append), use_remote_address_(false), 
      append_x_forwarded_port_(false) {}

Envoy::Http::OriginalIPDetectionResult
XffIPDetection::detect(Envoy::Http::OriginalIPDetectionParams& params) {
  // If use_remote_address is enabled, delegate to the specific handler
  if (use_remote_address_) {
    return handleUseRemoteAddressTrue(params);
  } else {
    return handleUseRemoteAddressFalse(params);
  }
}

void XffIPDetection::appendXff(Envoy::Http::RequestHeaderMap& request_headers, 
                               const Network::Address::Instance& remote_address) {
  if (remote_address.type() != Network::Address::Type::Ip) {
    return;
  }
  
  const std::string remote_ip = remote_address.ip()->addressAsString();
  
  // Check if this IP is already the last entry in XFF to prevent duplicates
  const auto xff_header = request_headers.ForwardedFor();
  if (xff_header != nullptr) {
    const std::string xff_value = std::string(xff_header->value().getStringView());
    
    // Find the last IP in the XFF header (after the last comma, or the whole string if no comma)
    size_t last_comma = xff_value.rfind(',');
    std::string last_ip;
    
    if (last_comma != std::string::npos) {
      // Extract everything after the last comma and trim whitespace
      last_ip = xff_value.substr(last_comma + 1);
      // Trim leading/trailing whitespace
      size_t start = last_ip.find_first_not_of(" \t");
      size_t end = last_ip.find_last_not_of(" \t");
      if (start != std::string::npos && end != std::string::npos) {
        last_ip = last_ip.substr(start, end - start + 1);
      }
    } else {
      // No comma, so the entire XFF value is the IP (trim whitespace)
      size_t start = xff_value.find_first_not_of(" \t");
      size_t end = xff_value.find_last_not_of(" \t");
      if (start != std::string::npos && end != std::string::npos) {
        last_ip = xff_value.substr(start, end - start + 1);
      }
    }
    
    // If the IP we're about to append is already the last one, skip appending
    if (last_ip == remote_ip) {
      return;
    }
  }
  
  // Not a duplicate, safe to append
  request_headers.appendForwardedFor(remote_ip, ",");
}

bool XffIPDetection::isLoopbackAddress(const Network::Address::Instance& address) {
  return Network::Utility::isLoopbackAddress(address);
}

Envoy::Http::OriginalIPDetectionResult
XffIPDetection::handleUseRemoteAddressTrue(Envoy::Http::OriginalIPDetectionParams& params) {
  const auto& downstream_remote_address = params.connection.connectionInfoProvider().remoteAddress();  
  Network::Address::InstanceConstSharedPtr final_remote_address;
  bool allow_trusted_address_checks = params.request_headers.ForwardedFor() == nullptr;
  if (!xff_trusted_cidrs_.empty()) {
    if (!Envoy::Http::Utility::remoteAddressIsTrustedProxy(*downstream_remote_address.get(),
                                                           xff_trusted_cidrs_)) {
      final_remote_address = downstream_remote_address;
    } else {
      auto ret = Envoy::Http::Utility::getLastNonTrustedAddressFromXFF(params.request_headers,
                                                                       xff_trusted_cidrs_);
      final_remote_address = ret.address_;
      allow_trusted_address_checks = ret.allow_trusted_address_checks_;
    }
  } else {
    if (xff_num_trusted_hops_ > 0) {
      final_remote_address =
          Envoy::Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_ - 1).address_;
    }
    if (final_remote_address == nullptr) {
      final_remote_address = downstream_remote_address;
    }
  }
  
  if (!skip_xff_append_) {
    if (isLoopbackAddress(*downstream_remote_address)) {
      appendXff(params.request_headers, *params.connection.streamInfo().downstreamAddressProvider().localAddress());
    } else {
      appendXff(params.request_headers, *downstream_remote_address);
    }
  }
  
  bool should_set_proto = false;
  if (!xff_trusted_cidrs_.empty()) {
    should_set_proto = !params.request_headers.ForwardedProto();
  } else {
    should_set_proto = (xff_num_trusted_hops_ == 0 || !params.request_headers.ForwardedProto());
  }
  
  if (should_set_proto) {
    const std::string proto = params.connection.ssl() ? "https" : "http";
    params.request_headers.setReferenceForwardedProto(proto);
  }
  
  bool should_set_port = false;
  if (!xff_trusted_cidrs_.empty()) {
    should_set_port = append_x_forwarded_port_ && !params.request_headers.ForwardedPort();
  } else {
    should_set_port = append_x_forwarded_port_ && 
                      (xff_num_trusted_hops_ == 0 || !params.request_headers.ForwardedPort());
  }
  
  if (should_set_port) {
    const auto* local_ip = params.connection.streamInfo().downstreamAddressProvider().localAddress()->ip();
    if (local_ip) {
      params.request_headers.setForwardedPort(local_ip->port());
    }
  }
  
  bool internal_request = allow_trusted_address_checks && final_remote_address && 
                          params.config.internalAddressConfig().isInternalAddress(*final_remote_address);
  bool edge_request = !internal_request; 
  
  if (edge_request && final_remote_address && final_remote_address->type() == Network::Address::Type::Ip) {
    params.request_headers.setEnvoyExternalAddress(final_remote_address->ip()->addressAsString());
  }

  return {final_remote_address, allow_trusted_address_checks, absl::nullopt, true};
}

Envoy::Http::OriginalIPDetectionResult 
XffIPDetection::handleUseRemoteAddressFalse(Envoy::Http::OriginalIPDetectionParams& params) {
  const auto& downstream_remote_address = params.connection.connectionInfoProvider().remoteAddress();
  
  if (!xff_trusted_cidrs_.empty()) {
    if (!Envoy::Http::Utility::remoteAddressIsTrustedProxy(*downstream_remote_address.get(),
                                                           xff_trusted_cidrs_)) {
      return {nullptr, false, absl::nullopt, false};
    }
    auto ret = Envoy::Http::Utility::getLastNonTrustedAddressFromXFF(params.request_headers,
                                                                     xff_trusted_cidrs_);
    return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt, skip_xff_append_};
  }

  auto ret =
      Envoy::Http::Utility::getLastAddressFromXFF(params.request_headers, xff_num_trusted_hops_);
  return {ret.address_, ret.allow_trusted_address_checks_, absl::nullopt, skip_xff_append_};
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
