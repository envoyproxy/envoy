#pragma once

#include "envoy/thread_local/thread_local.h"
#include "source/common/common/matchers.h"
#include "source/extensions/filters/common/lua/lua.h"

namespace Envoy::Extensions::Matching::String::Lua {

class LuaStringMatcher : public Matchers::StringMatcher, public ThreadLocal::ThreadLocalObject {
public:
  LuaStringMatcher(const std::string& code);

  // ThreadLocal::ThreadLocalObject
  ~LuaStringMatcher() override = default;

  // Matchers::StringMatcher
  bool match(const absl::string_view value) const override;

private:
  CSmartPtr<lua_State, lua_close> state_;
};

} // namespace Envoy::Extensions::Matching::String::Lua
