#include "common/lua/wrappers.h"

namespace Envoy {
namespace Lua {

int BufferWrapper::luaLength(lua_State* state) {
  lua_pushnumber(state, data_.length());
  return 1;
}

int BufferWrapper::luaGetBytes(lua_State* state) {
  const int index = luaL_checkint(state, 2);
  const int length = luaL_checkint(state, 3);
  if (index < 0 || length < 0 ||
      static_cast<uint64_t>(index) + static_cast<uint64_t>(length) > data_.length()) {
    luaL_error(state, "index/length must be >= 0 and (index + length) must be <= buffer size");
  }

  // TODO(mattklein123): Reduce copies here by using Lua direct buffer builds.
  std::unique_ptr<char[]> data(new char[length]);
  data_.copyOut(index, length, data.get());
  lua_pushlstring(state, data.get(), length);
  return 1;
}

} // namespace Lua
} // namespace Envoy
