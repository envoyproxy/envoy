#include "source/common/common/matchers.h"

#include "envoy/extensions/matching/string/lua/v3/lua.pb.h"

namespace Envoy::Extensions::Matching::String::Lua {

class LuaStringMatcherFactory : public Matchers::StringMatcherExtensionFactory {
public:
  Matchers::StringMatcherPtr
  createStringMatcher(const ::xds::core::v3::TypedExtensionConfig& config) override {
    auto typed_config =
        dynamic_cast<::envoy::extensions::matching::string::lua::v3::Lua>(config.typed_config());
    return nullptr;
  }

  std::string name() const override { return "envoy.matching.string.lua"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<::envoy::extensions::matching::string::lua::v3::Lua>();
  }
};

REGISTER_FACTORY(LuaStringMatcherFactory, Matchers::StringMatcherExtensionFactory);

} // namespace Envoy::Extensions::Matching::String::Lua
