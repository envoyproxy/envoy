#pragma once

#include "envoy/thread_local/thread_local.h"

#include "source/common/common/matchers.h"
#include "source/extensions/filters/common/lua/lua.h"

namespace Envoy {
namespace Extensions {
namespace StringMatcher {
namespace Lua {

// This class should not be used directly. It is exposed here for use in tests.
// Correct use requires use of a thread local slot.
class LuaStringMatcher : public Matchers::StringMatcher, public ThreadLocal::ThreadLocalObject {
public:
  LuaStringMatcher(const std::string& code);

  // ThreadLocal::ThreadLocalObject
  ~LuaStringMatcher() override = default;

  // Matchers::StringMatcher
  bool match(const absl::string_view value) const override;

private:
  CSmartPtr<lua_State, lua_close> state_;
  int matcher_func_ref_{LUA_NOREF};
};

class LuaStringMatcherFactory : public Matchers::StringMatcherExtensionFactory {
public:
  Matchers::StringMatcherPtr
  createStringMatcher(const Protobuf::Message& config,
                      Server::Configuration::CommonFactoryContext& context) override;
  std::string name() const override { return "envoy.string_matcher.lua"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override;
};

} // namespace Lua
} // namespace StringMatcher
} // namespace Extensions
} // namespace Envoy
