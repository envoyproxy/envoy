#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "source/common/common/macros.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * TwitterProtocolImpl implements the Twitter-upgraded (AKA "TTwitter") Thrift protocol.
 * See https://twitter.github.io/finagle/docs/com/twitter/finagle/Thrift$ and
 * https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/thrift/tracing.thrift
 */
class TwitterProtocolImpl : public BinaryProtocolImpl {
public:
  // Protocol
  const std::string& name() const override { return ProtocolNames::get().TWITTER; }
  ProtocolType type() const override { return ProtocolType::Twitter; }
  bool readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  void writeMessageBegin(Buffer::Instance& buffer, const MessageMetadata& metadata) override;
  bool supportsUpgrade() override { return true; }
  DecoderEventHandlerSharedPtr upgradeRequestDecoder() override;
  DirectResponsePtr upgradeResponse(const DecoderEventHandler& decoder) override;
  ThriftObjectPtr attemptUpgrade(Transport& transport, ThriftConnectionState& state,
                                 Buffer::Instance& buffer) override;
  void completeUpgrade(ThriftConnectionState& state, ThriftObject& response) override;

  /**
   * @return true if the protocol upgrade was success, false if not, no value if the result is not
   *         yet known
   */
  absl::optional<bool> upgraded() { return upgraded_; }

  /**
   * @return std::string containing the "improbably-named method" used for Twitter protocol upgrade.
   */
  static const std::string& upgradeMethodName() {
    // https://github.com/twitter/finagle/blob/master/finagle-thrift/src/main/scala/com/twitter/finagle/thrift/ThriftTracing.scala
    CONSTRUCT_ON_FIRST_USE(std::string, "__can__finagle__trace__v3__");
  }

  /**
   * @return true if the buffer (minimum 12 bytes) appears to start with a Twitter protocol
   *         upgrade message, false otherwise
   */
  static bool isUpgradePrefix(Buffer::Instance& buffer);

protected:
  static void updateMetadataWithRequestHeader(const ThriftObject& header_object,
                                              MessageMetadata& metadata);
  static void updateMetadataWithResponseHeader(const ThriftObject& header_object,
                                               MessageMetadata& metadata);
  static void writeRequestHeader(Buffer::Instance& buffer, const MessageMetadata& metadata);
  static void writeResponseHeader(Buffer::Instance& buffer, const MessageMetadata& metadata);
  static ThriftObjectPtr newHeader();

private:
  ThriftObjectPtr header_;
  bool header_complete_{false};
  absl::optional<bool> upgraded_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
