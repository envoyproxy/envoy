#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

DirectResponse::ResponseType AppException::encode(MessageMetadata& metadata,
                                                  Buffer::Instance& buffer) const {
  std::string output = "";

  // Top line
  output += "SIP/2.0 503 Service Unavaliable\r\n";

  // To
  if (!absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::To]).empty()) {
    auto to = absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::To])[0];
    output += fmt::format("{}: {}", "To", to);

    if (to.find("tag=") == absl::string_view::npos) {

      // We could simply use the time of day as a tag; however, that is not unique
      // enough. So, let's perturb the time of day with a salt to get a better
      // unique number. The salt I am using here is the summation of each
      // character of the proxy's IP address
      output += ";tag=";
      if (metadata.ep().has_value() && metadata.ep().value().length() > 0) {
        output += fmt::format("{}-", metadata.ep().value());
      }
      std::time_t t;
      long s = 0;
      char buf[80];
      t = time(&t);
      s = std::labs(t - s);
      sprintf(buf, "%lx", s);
      output += buf;
    }
    output += "\r\n";
  } else {
    ENVOY_LOG(error, "No \"To\" in received message");
  }

  // From
  if (!absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::From]).empty()) {
    output += fmt::format("{}: {}\r\n", "From",
                          absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::From])[0]);
  } else {
    ENVOY_LOG(error, "No \"From\" in received message");
  }

  // Call-ID
  if (!absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::From]).empty()) {
    output += fmt::format("{}: {}\r\n", "Call-ID",
                          absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::CallId])[0]);
  } else {
    ENVOY_LOG(error, "No \"Call-ID\" in received message");
  }

  // Via
  for (auto via : absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::Via])) {
    output += fmt::format("{}: {}\r\n", "Via", via);
  }

  // CSeq
  if (!absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::From]).empty()) {
    output += fmt::format("{}: {}\r\n", "CSeq",
                          absl::get<VectorHeader>(metadata.msgHeaderList()[HeaderType::Cseq])[0]);
  } else {
    ENVOY_LOG(error, "No \"Cseq\" in received message");
  }

  // Failed Reason
  output += fmt::format("{}: {}\r\n", "Reason", what());

  // Content-length
  output += "Content-Length: 0\r\n";

  // End
  output += "\r\n";

  buffer.add(output);

  return DirectResponse::ResponseType::Exception;
}

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
