#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

DirectResponse::ResponseType AppException::encode(MessageMetadata& metadata,
                                                  Buffer::Instance& buffer) const {
  std::stringstream output;

  // Top line
  output << "SIP/2.0 503 Service Unavailable\r\n";

  // To
  if (!metadata.header(HeaderType::To).empty()) {
    metadata.parseHeader(HeaderType::To);
    auto to = metadata.header(HeaderType::To);
    output << fmt::format("To: {}", to.text());

    if (!to.hasParam("tag")) {

      // We could simply use the time of day as a tag; however, that is not unique
      // enough. So, let's perturb the time of day with a salt to get a better
      // unique number. The salt I am using here is the summation of each
      // character of the proxy's IP address
      output << ";tag=";
      if (metadata.ep().has_value() && metadata.ep().value().length() > 0) {
        output << fmt::format("{}-", metadata.ep().value());
      }
      std::time_t t;
      long s = 0;
      t = time(&t);
      s = std::labs(t - s);
      output << fmt::format("{}", s);
    }
    output << "\r\n";
  } else {
    ENVOY_LOG(error, "No \"To\" in received message");
  }

  // From
  if (!metadata.header(HeaderType::From).empty()) {
    output << fmt::format("From: {}\r\n", metadata.header(HeaderType::From).text());
  } else {
    ENVOY_LOG(error, "No \"From\" in received message");
  }

  // Call-ID
  if (!metadata.header(HeaderType::CallId).empty()) {
    output << fmt::format("Call-ID: {}\r\n", metadata.header(HeaderType::CallId).text());
  } else {
    ENVOY_LOG(error, "No \"Call-ID\" in received message");
  }

  // Via
  for (const auto& via : metadata.listHeader(HeaderType::Via)) {
    output << fmt::format("Via: {}\r\n", via.text());
  }

  // CSeq
  if (!metadata.header(HeaderType::Cseq).empty()) {
    output << fmt::format("CSeq: {}\r\n", metadata.header(HeaderType::Cseq).text());
  } else {
    ENVOY_LOG(error, "No \"Cseq\" in received message");
  }

  // Failed Reason
  output << fmt::format("Reason: {}\r\n", what());

  // Content-length
  output << "Content-Length: 0\r\n";

  // End
  output << "\r\n";

  buffer.add(output.str());

  return DirectResponse::ResponseType::Exception;
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
