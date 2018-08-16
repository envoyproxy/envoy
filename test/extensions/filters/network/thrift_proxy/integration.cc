#include "test/extensions/filters/network/thrift_proxy/integration.h"

#include <algorithm>
#include <fstream>

#include "test/test_common/environment.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

const std::string& PayloadOptions::modeName() const {
  static const std::string success = "success";
  static const std::string idl_exception = "idl-exception";
  static const std::string exception = "exception";

  switch (mode_) {
  case DriverMode::Success:
    return success;
  case DriverMode::IDLException:
    return idl_exception;
  case DriverMode::Exception:
    return exception;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

const std::string& PayloadOptions::transportName() const {
  static const std::string framed = "framed";
  static const std::string unframed = "unframed";
  static const std::string header = "header";

  switch (transport_) {
  case TransportType::Framed:
    return framed;
  case TransportType::Unframed:
    return unframed;
  case TransportType::Header:
    return header;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

const std::string& PayloadOptions::protocolName() const {
  static const std::string binary = "binary";
  static const std::string compact = "compact";

  switch (protocol_) {
  case ProtocolType::Binary:
    return binary;
  case ProtocolType::Compact:
    return compact;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

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
    args.push_back(*options.service_name_);
  }
  args.push_back("--");
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

  std::ifstream is(file, std::ios::binary | std::ios::ate);
  RELEASE_ASSERT(!is.fail(), "");

  std::ifstream::pos_type len = is.tellg();
  if (len > 0) {
    std::vector<char> bytes(len, 0);
    is.seekg(0, std::ios::beg);
    RELEASE_ASSERT(!is.fail(), "");

    is.read(bytes.data(), len);
    RELEASE_ASSERT(!is.fail(), "");

    buffer.add(bytes.data(), len);
  }
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
