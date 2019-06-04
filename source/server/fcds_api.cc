#include "server/fcds_api.h"

#include <unordered_map>

#include "envoy/api/v2/lds.pb.validate.h"
#include "envoy/api/v2/listener/listener.pb.h"
#include "envoy/api/v2/listener/listener.pb.validate.h"
#include "envoy/config/subscription_factory.h"
#include "envoy/stats/scope.h"

#include "common/common/cleanup.h"
#include "common/config/resources.h"
#include "common/config/utility.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {

std::string FcdsApiFromLds::resourceName(const ProtobufWkt::Any& resource) {
  return MessageUtil::anyConvert<envoy::api::v2::listener::FilterChain>(resource,
                                                                        validation_visitor_)
      .name();
}

void FcdsApiFromLds::onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>&,
                                    const Protobuf::RepeatedPtrField<std::string>&,
                                    const std::string&) {}

void FcdsApiFromLds::onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>&,
                                    const std::string&) {}

void FcdsApiFromLds::onConfigUpdateFailed(const EnvoyException*) {}

} // namespace Server
} // namespace Envoy