#include "common/http/filter/lua/wrappers.h"

namespace Envoy {
namespace Http {
namespace Filter {
namespace Lua {

int HeaderMapWrapper::luaAdd(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  const char* value = luaL_checkstring(state, 3);
  headers_.addCopy(LowerCaseString(key), value);
  return 0;
}

int HeaderMapWrapper::luaGet(lua_State* state) {
  const char* key = luaL_checkstring(state, 2);
  const HeaderEntry* entry = headers_.get(LowerCaseString(key));
  if (entry != nullptr) {
    lua_pushstring(state, entry->value().c_str());
    return 1;
  } else {
    return 0;
  }
}

int HeaderMapWrapper::luaIterate(lua_State* state) {
  luaL_checktype(state, 2, LUA_TFUNCTION);
  headers_.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        // Push a duplicate of the function and the params onto the stack and then call.
        lua_State* state = static_cast<lua_State*>(context);
        lua_pushvalue(state, -1);
        lua_pushstring(state, header.key().c_str());
        lua_pushstring(state, header.value().c_str());

        // Do not use pcall(). Errors will throw all the way back to the original resume().
        lua_call(state, 2, 0);
        return HeaderMap::Iterate::Continue;
      },
      state);

  return 0;
}

int HeaderMapWrapper::luaRemove(lua_State* state) {
  checkModifiable(state);

  const char* key = luaL_checkstring(state, 2);
  headers_.remove(LowerCaseString(key));
  return 0;
}

} // namespace Lua
} // namespace Filter
} // namespace Http
} // namespace Envoy
