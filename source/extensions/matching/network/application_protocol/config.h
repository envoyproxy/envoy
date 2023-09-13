#pragma once

#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.h"
#include "envoy/extensions/matching/common_inputs/network/v3/network_inputs.pb.validate.h"
#include "envoy/matcher/matcher.h"
#include "envoy/network/filter.h"
#include "envoy/registry/registry.h"

#include "source/common/network/utility.h"
#include "source/extensions/matching/network/common/inputs.h"

namespace Envoy {
namespace Network {
namespace Matching {

class ApplicationProtocolInput : public Matcher::DataInput<MatchingData> {
public:
  Matcher::DataInputGetResult get(const MatchingData& data) const override;
};

class ApplicationProtocolInputFactory
    : public BaseFactory<
          ApplicationProtocolInput,
          envoy::extensions::matching::common_inputs::network::v3::ApplicationProtocolInput,
          MatchingData> {
public:
  ApplicationProtocolInputFactory() : BaseFactory("application_protocol") {}
};

DECLARE_FACTORY(ApplicationProtocolInputFactory);

} // namespace Matching
} // namespace Network
} // namespace Envoy
