#pragma once

#include <string>
#include <vector>

#include "extensions/filters/network/thrift_proxy/thrift.h"

#include "test/integration/integration.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * DriverMode represents the modes the test driver server modes.
 */
enum class DriverMode {
  // Server returns successful responses.
  Success,

  // Server throws IDL-defined exceptions.
  IDLException,

  // Server throws application exceptions.
  Exception,
};

struct PayloadOptions {
  PayloadOptions(TransportType transport, ProtocolType protocol, DriverMode mode,
                 absl::optional<std::string> service_name, std::string method_name,
                 std::vector<std::string> method_args = {},
                 std::vector<std::pair<std::string, std::string>> headers = {})
      : transport_(transport), protocol_(protocol), mode_(mode), service_name_(service_name),
        method_name_(method_name), method_args_(method_args), headers_(headers) {}

  std::string modeName() const;
  std::string transportName() const;
  std::string protocolName() const;

  const TransportType transport_;
  const ProtocolType protocol_;
  const DriverMode mode_;
  const absl::optional<std::string> service_name_;
  const std::string method_name_;
  const std::vector<std::string> method_args_;
  const std::vector<std::pair<std::string, std::string>> headers_;
};

class BaseThriftIntegrationTest : public BaseIntegrationTest {
public:
  BaseThriftIntegrationTest()
      : BaseIntegrationTest(Network::Address::IpVersion::v4, realTime(), thrift_config_) {}

  /**
   * Given PayloadOptions, generate a client request and server response and store the
   * data in the given Buffers.
   */
  void preparePayloads(const PayloadOptions& options, Buffer::Instance& request_buffer,
                       Buffer::Instance& response_buffer);

protected:
  // Tests should use a static SetUpTestCase method to initialize this field with a suitable
  // configuration.
  static std::string thrift_config_;

private:
  void readAll(std::string file, Buffer::Instance& buffer);
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
