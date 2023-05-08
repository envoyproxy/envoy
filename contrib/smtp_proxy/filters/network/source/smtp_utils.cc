#include "contrib/smtp_proxy/filters/network/source/smtp_utils.h"

#include <sstream>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SmtpProxy {

// Function to generate SMTP response with enhanced status code
std::string SmtpUtils::generateResponse(int code, EnhancedCode enhCode, std::string text) {
  std::ostringstream response;

  // All responses must include an enhanced code, if it is missing - use
  // a generic code X.0.0.
  if (enhCode == EnhancedCodeNotSet) {
    int cat = code / 100;
    switch (cat) {
    case 2:
    case 4:
    case 5:
      enhCode = {cat, 0, 0};
      break;
    default:
      enhCode = EnhancedCodeNotSet;
      break;
    }
  }

  // for (auto it = text.begin(); it != text.end() - 1; ++it) {
  //     response << code << "- " << *it << "\r\n";
  // }

  if (enhCode == EnhancedCodeNotSet) {
    response << code << " " << text << "\r\n";
  } else {
    response << code << " " << enhCode[0] << "." << enhCode[1] << "." << enhCode[2] << " " << text
             << "\r\n";
  }

  return response.str();
}

// Extract the email address between the < and > characters.

std::string SmtpUtils::extractAddress(std::string& arg) {
  std::string address = "";
  size_t start_pos = arg.find('<');
  size_t end_pos = arg.find('>');

  if ((start_pos != std::string::npos && end_pos != std::string::npos) && (start_pos < end_pos)) {
    address = arg.substr(start_pos + 1, end_pos - start_pos - 1);
  }
  return address;
}

} // namespace SmtpProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
