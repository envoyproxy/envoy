#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace Actions {
namespace FormatString {

// A `StringReturningAction` factory is registered for `SubstitutionFormatString`
// and for `Protobuf::StringValue` action configurations.
struct StringReturningActionFactoryContext {
  // A ServerFactoryContext is necessary to initialize a SubstitutionFormatString.
  Server::Configuration::ServerFactoryContext& server_factory_context_;
};

class StringReturningAction : public Matcher::Action {
public:
  virtual std::string string(const StreamInfo::StreamInfo& stream_info) const PURE;
};

// A `FilterChainBaseAction` factory is registered for `SubstitutionFormatString`
// action configurations.
using FilterChainActionFactoryContext = Server::Configuration::ServerFactoryContext;

} // namespace FormatString
} // namespace Actions
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
