#pragma once

#include "envoy/request_id_utils/request_id_utils.h"
#include "envoy/server/request_id_utils_config.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace RequestIDUtils {
namespace UUID {

/**
 * Config registration for the UUID based RequestID Utilities. @see RequestIDUtilsFactory.
 */
class UUIDUtilsFactory : public Server::Configuration::RequestIDUtilsFactory {
public:
  Envoy::RequestIDUtils::UtilitiesSharedPtr
  createUtilitiesInstance(const Protobuf::Message& config,
                          Server::Configuration::FactoryContext& context) override;

  std::string name() const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override;

  std::string category() const override;
};

} // namespace UUID
} // namespace RequestIDUtils
} // namespace Extensions
} // namespace Envoy
