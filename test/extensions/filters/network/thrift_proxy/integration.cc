#include "test/extensions/filters/network/thrift_proxy/integration.h"

#include <algorithm>
#include <fstream>

#include "common/filesystem/filesystem_impl.h"

#include "test/test_common/environment.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

std::string PayloadOptions::modeName() const {
  switch (mode_) {
  case DriverMode::Success:
    return "success";
  case DriverMode::IDLException:
    return "idl-exception";
  case DriverMode::Exception:
    return "exception";
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::string PayloadOptions::transportName() const {
  switch (transport_) {
  case TransportType::Framed:
    return "framed";
  case TransportType::Unframed:
    return "unframed";
  case TransportType::Header:
    return "header";
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::string PayloadOptions::protocolName() const {
  switch (protocol_) {
  case ProtocolType::Binary:
    return "binary";
  case ProtocolType::Compact:
    return "compact";
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

std::string BaseThriftIntegrationTest::thrift_config_;

void BaseThriftIntegrationTest::preparePayloads(const PayloadOptions& options,
                                                Buffer::Instance& request_buffer,
                                                Buffer::Instance& response_buffer) {
  std::vector<std::string> args = {
      TestEnvironment::runfilesPath(
          "test/extensions/filters/network/thrift_proxy/driver/generate_fixture.sh"),
      options.modeName(),
      options.transportName(),
      options.protocolName(),
  };

  if (options.service_name_) {
    args.push_back("-s");
    args.push_back(*options.service_name_);
  }

  if (options.headers_.size() > 0) {
    args.push_back("-H");

    std::vector<std::string> headers;
    std::transform(options.headers_.begin(), options.headers_.end(), std::back_inserter(headers),
                   [](const std::pair<std::string, std::string>& header) -> std::string {
                     return header.first + "=" + header.second;
                   });
    args.push_back(StringUtil::join(headers, ","));
  }

  args.push_back(options.method_name_);
  std::copy(options.method_args_.begin(), options.method_args_.end(), std::back_inserter(args));

  TestEnvironment::exec(args);

  std::stringstream file_base;
  file_base << "{{ test_tmpdir }}/" << options.transportName() << "-" << options.protocolName()
            << "-";
  if (options.service_name_) {
    file_base << *options.service_name_ << "-";
  }
  file_base << options.modeName();

  readAll(file_base.str() + ".request", request_buffer);
  readAll(file_base.str() + ".response", response_buffer);
}

void BaseThriftIntegrationTest::readAll(std::string file, Buffer::Instance& buffer) {
  file = TestEnvironment::substitute(file, version_);

  std::string data = Filesystem::fileReadToEnd(file);
  buffer.add(data);
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
