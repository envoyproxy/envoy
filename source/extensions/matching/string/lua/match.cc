#include "source/extensions/matching/string/lua/match.h"

#include "envoy/extensions/matching/string/lua/v3/lua.pb.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"

namespace Envoy::Extensions::Matching::String::Lua {

LuaStringMatcher::LuaStringMatcher(const std::string& code) : state_(luaL_newstate()) {
  RELEASE_ASSERT(state_.get() != nullptr, "unable to create new Lua state object");
  luaL_openlibs(state_.get());
  int rc = luaL_dostring(state_.get(), code.c_str());
  if (rc != 0) {
    absl::string_view error("unknown");
    if (lua_isstring(state_.get(), -1)) {
      size_t len = 0;
      const char* err = lua_tolstring(state_.get(), -1, &len);
      error = absl::string_view(err, len);
    }
    throw EnvoyException(absl::StrCat("Failed to load lua code in Lua StringMatcher:", error));
  }

  lua_getglobal(state_.get(), "match");
  bool is_function = lua_isfunction(state_.get(), -1);
  lua_pop(state_.get(), 1);
  if (!is_function) {
    throw EnvoyException("Lua code did not contain a global function named 'match'");
  }
}

bool LuaStringMatcher::match(const absl::string_view value) const {
  const int initial_depth = lua_gettop(state_.get());

  bool ret = [&]() {
    lua_getglobal(state_.get(), "match");
    ASSERT(lua_isfunction(state_.get(), -1)); // Validated in constructor

    lua_pushlstring(state_.get(), value.data(), value.size());
    int rc = lua_pcall(state_.get(), 1, 1, 0);
    if (rc != 0) {
      // Runtime error
      absl::string_view error("unknown");
      if (lua_isstring(state_.get(), -1)) {
        size_t len = 0;
        const char* err = lua_tolstring(state_.get(), -1, &len);
        error = absl::string_view(err, len);
      }
      ENVOY_LOG_PERIODIC_MISC(error, std::chrono::seconds(5),
                              "Lua StringMatcher error running script: {}", error);
      lua_pop(state_.get(), 1);

      return false;
    }

    bool ret = false;
    if (lua_isboolean(state_.get(), -1)) {
      ret = lua_toboolean(state_.get(), -1) != 0;
    } else {
      ENVOY_LOG_PERIODIC_MISC(error, std::chrono::seconds(5),
                              "Lua StringMatcher match function did not return a boolean");
    }

    lua_pop(state_.get(), 1);
    return ret;
  }();

  // Validate that the stack is restored to it's original state; nothing added or removed.
  ASSERT(lua_gettop(state_.get()) == initial_depth);
  return ret;
}

// Lua state is not thread safe, so a state needs to be stored in thread local storage.
class LuaStringMatcherThreadWrapper : public Matchers::StringMatcher {
public:
  LuaStringMatcherThreadWrapper(const std::string& code) {
    // Validate that there are no errors while creating on the main thread.
    LuaStringMatcher validator(code);

    tls_slot_ = ThreadLocal::TypedSlot<LuaStringMatcher>::makeUnique(
        *InjectableSingleton<ThreadLocal::SlotAllocator>::getExisting());
    tls_slot_->set([code](Event::Dispatcher&) -> std::shared_ptr<LuaStringMatcher> {
      return std::make_shared<LuaStringMatcher>(code);
    });
  }

  bool match(const absl::string_view value) const override { return (*tls_slot_)->match(value); }

private:
  ThreadLocal::TypedSlotPtr<LuaStringMatcher> tls_slot_;
};

class LuaStringMatcherFactory : public Matchers::StringMatcherExtensionFactory {
public:
  Matchers::StringMatcherPtr createStringMatcher(const ProtobufWkt::Any& message) override {
    ::envoy::extensions::matching::string::lua::v3::Lua config;
    Config::Utility::translateOpaqueConfig(message, ProtobufMessage::getStrictValidationVisitor(),
                                           config);
    return std::make_unique<LuaStringMatcherThreadWrapper>(config.inline_code());
  }

  std::string name() const override { return "envoy.matching.string.lua"; }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<::envoy::extensions::matching::string::lua::v3::Lua>();
  }
};

REGISTER_FACTORY(LuaStringMatcherFactory, Matchers::StringMatcherExtensionFactory);

} // namespace Envoy::Extensions::Matching::String::Lua
