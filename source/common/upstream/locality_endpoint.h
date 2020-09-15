#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

#include "common/protobuf/utility.h"

namespace Envoy {
namespace Upstream {

using LocalityEndpointTuple = std::tuple<const envoy::config::core::v3::Locality&,
                                         const envoy::config::endpoint::v3::LbEndpoint&>;
struct LocalityEndpointHash {
  size_t operator()(const LocalityEndpointTuple& values) const {
    const envoy::config::core::v3::Locality& locality = std::get<0>(values);
    const envoy::config::endpoint::v3::LbEndpoint& endpoint = std::get<1>(values);

    std::string text;
    {
      // For memory safety, the StringOutputStream needs to be destroyed before
      // we read the string.
      Protobuf::io::StringOutputStream string_stream(&text);
      Protobuf::io::CodedOutputStream coded_stream(&string_stream);
      coded_stream.SetSerializationDeterministic(true);
      locality.SerializeToCodedStream(&coded_stream);
      endpoint.SerializeToCodedStream(&coded_stream);
    }
    return HashUtil::xxHash64(text);
  }
};

struct LocalityEndpointEqualTo {
  bool operator()(const LocalityEndpointTuple& lhs, const LocalityEndpointTuple& rhs) const {
    return Protobuf::util::MessageDifferencer::Equals(std::get<0>(lhs), std::get<0>(rhs)) &&
           Protobuf::util::MessageDifferencer::Equals(std::get<1>(lhs), std::get<1>(rhs));
  }
};

} // namespace Upstream
} // namespace Envoy
