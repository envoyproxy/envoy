#pragma once

#include "envoy/matcher/matcher.h"
#include "envoy/server/factory_context.h"

#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Matcher {
namespace Actions {

// A `StringReturningAction` factory is registered for `SubstitutionFormatString`
// and for `Protobuf::StringValue` action configurations.
struct StringReturningActionFactoryContext {
  // A ServerFactoryContext is necessary to initialize a SubstitutionFormatString.
  Server::Configuration::ServerFactoryContext& server_factory_context_;
};

class StringReturningAction : public Matcher::Action {
public:
  virtual std::string getOutputString(const StreamInfo::StreamInfo& stream_info) const PURE;
};

} // namespace Actions
} // namespace Matcher
} // namespace Envoy
