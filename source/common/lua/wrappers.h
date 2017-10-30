#pragma once

#include "envoy/buffer/buffer.h"

#include "common/lua/lua.h"

namespace Envoy {
namespace Lua {

/**
 * A wrapper for a constant buffer which cannot be modified by Lua.
 */
class BufferWrapper : public BaseLuaObject<BufferWrapper> {
public:
  BufferWrapper(const Buffer::Instance& data) : data_(data) {}

  static ExportedFunctions exportedFunctions() {
    return {{"length", static_luaLength}, {"getBytes", static_luaGetBytes}};
  }

private:
  /**
   * @return int the size in bytes of the buffer.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaLength);

  /**
   * Get bytes out of a buffer for inspection in Lua.
   * @param 1 (int) starting index of bytes to extract.
   * @param 2 (int) length of bytes to extract.
   * @return string the extracted bytes. Throws an error if the index/length are out of range.
   */
  DECLARE_LUA_FUNCTION(BufferWrapper, luaGetBytes);

  const Buffer::Instance& data_;
};

} // namespace Lua
} // namespace Envoy
