#pragma once

#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/logger.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "lua.hpp"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Lua {

/**
 * Some general notes on everything in this file. Lua/C bindings are functional, but not the most
 * beautiful interfaces. For more general overview information see the following:
 * 1) https://www.lua.org/manual/5.1/manual.html#3
 * 2) https://doc.lagout.org/programmation/Lua/Programming%20in%20Lua%20Second%20Edition.pdf
 * 3) http://luajit.org/extensions.html
 *
 * Instead of delving into crazy template metaprogramming in all cases, I've tried to use a mix
 * of templates and macros to try to hide the majority of the pain. I.e., most Lua/C pain should
 * be in this file. We do still expose basic Lua/C programming (manipulating the stack, etc.) out
 * to callers which avoids the messy C++ template programming I mentioned above.
 */

/**
 * A fixed-size, trivially destructible holder for a Lua error message.
 *
 * Raising a Lua error (luaL_error()/lua_error()) does not return: LuaJIT unwinds the C++ stack
 * back to the enclosing lua_resume()/lua_pcall(). Whether the destructors of the C++ objects that
 * are live in the frames being unwound get run depends on the toolchain emitting exception
 * cleanups, so error paths must not rely on it. Copying the message into one of these lets the
 * caller destroy every non-trivial object it owns before it raises. Messages longer than
 * MaxLength are truncated.
 */
class LuaErrorMessage {
public:
  static constexpr size_t MaxLength = 512;

  // The buffer is deliberately left uninitialized apart from the terminator: the thunk generated
  // by DECLARE_LUA_FUNCTION_EX() declares one of these on every Lua call, and clearing the whole
  // buffer on the success path would be pure overhead.
  LuaErrorMessage() { buffer_[0] = '\0'; }

  void set(const absl::Status& status) { set(status.message()); }

  void set(absl::string_view message) {
    const size_t length = message.copy(buffer_, MaxLength - 1);
    buffer_[length] = '\0';
  }

  const char* c_str() const { return buffer_; }

private:
  char buffer_[MaxLength];
};

static_assert(std::is_trivially_destructible<LuaErrorMessage>::value,
              "LuaErrorMessage must be trivially destructible so that it can be held live across "
              "the call that raises the Lua error");

/**
 * Base macro for declaring a Lua/C function. Any function declared will need to be exported via
 * the exportedFunctions() function in BaseLuaObject. See BaseLuaObject below for more
 * information. This macro declares a static "thunk" which checks the user data, optionally checks
 * for object death (again see BaseLuaObject below for more info), and then invokes a normal
 * object method. The actual object method needs to be implemented by the class.
 *
 * The object method returns absl::StatusOr<int> rather than raising the Lua error itself: on
 * success the int is the number of values the method pushed onto the Lua stack, and on failure
 * the status message becomes the Lua error. Raising a Lua error unwinds the C++ stack (see
 * LuaErrorMessage above), so the thunk copies the message into a plain buffer and lets every C++
 * object in scope be destroyed *before* it calls luaL_error(). That way the error paths do not
 * depend on the unwinder running destructors for us.
 * @param Class supplies the owning class name.
 * @param Name supplies the function name.
 * @param Index supplies the stack index where "this" (Lua/C userdata) is found.
 */
#define DECLARE_LUA_FUNCTION_EX(Class, Name, Index)                                                \
  static int static_##Name(lua_State* state) {                                                     \
    ::Envoy::Extensions::Filters::Common::Lua::LuaErrorMessage error_message;                      \
    {                                                                                              \
      Class* object = ::Envoy::Extensions::Filters::Common::Lua::alignAndCast<Class>(              \
          luaL_checkudata(state, Index, typeid(Class).name()));                                    \
      const absl::Status dead_status = object->checkDead();                                        \
      if (dead_status.ok()) {                                                                      \
        const absl::StatusOr<int> result = object->Name(state);                                    \
        if (result.ok()) {                                                                         \
          return *result;                                                                          \
        }                                                                                          \
        error_message.set(result.status());                                                        \
      } else {                                                                                     \
        error_message.set(dead_status);                                                            \
      }                                                                                            \
    }                                                                                              \
    /* Nothing with a destructor is live here, so it is safe for luaL_error() to unwind. */        \
    return luaL_error(state, "%s", error_message.c_str());                                         \
  }                                                                                                \
  absl::StatusOr<int> Name(lua_State* state);

/**
 * Declare a Lua function in which userdata is in stack slot 1. See DECLARE_LUA_FUNCTION_EX()
 */
#define DECLARE_LUA_FUNCTION(Class, Name) DECLARE_LUA_FUNCTION_EX(Class, Name, 1)

/**
 * Declare a Lua function in which userdata is in upvalue slot 1. See DECLARE_LUA_FUNCTION_EX()
 */
#define DECLARE_LUA_CLOSURE(Class, Name) DECLARE_LUA_FUNCTION_EX(Class, Name, lua_upvalueindex(1))

/**
 * Declare a Lua function in which values are added to a table to approximate an enum.
 */
#define LUA_ENUM(state, name, val)                                                                 \
  lua_pushlstring(state, #name, sizeof(#name) - 1);                                                \
  lua_pushnumber(state, val);                                                                      \
  lua_settable(state, -3);

/**
 * Non-raising counterparts of the luaL_check*()/luaL_opt*() family.
 *
 * Raising a Lua error unwinds the C++ stack (see LuaErrorMessage above), so a lua*() method body
 * must never call a function that raises while it owns anything that needs destroying. Rather than
 * ask every reader to reason about which locals happen to be live at each argument read -- an
 * invariant nothing can check and which has already been broken twice in this extension -- nothing
 * here raises. luaL_check*()/luaL_opt*() are banned in these files by tools/code_format; use these
 * instead.
 *
 * There are two families:
 *
 * 1) The "arg" family below reads a *function argument* at a positive stack index and reproduces
 *    LuaJIT's own message verbatim, e.g. "bad argument #1 to 'add' (string expected, got boolean)".
 *    Use it wherever luaL_check*()/luaL_opt*() was used, so the errors a script author sees are
 *    unchanged. Each takes the name the method is exported under, which the caller knows
 *    statically.
 *
 * 2) The stringOrError()/coercibleStringOrError()/integerOrError() family further down reads a
 *    value that is *not* an argument -- a table key or entry reached by lua_next(), say -- and
 *    takes a caller-supplied label, because "bad argument #2" would misdescribe it.
 */

/**
 * Build the message LuaJIT's own argument errors carry, without raising it.
 *
 * @param state the current Lua state.
 * @param index the stack index the argument was read from. Slot 1 holds the receiver of the
 *        method call, so real arguments start at 2 and the number reported is one less -- the
 *        same adjustment err_argmsg() in LuaJIT's lj_err.c makes for a method call. LuaJIT's
 *        separate "calling 'f' on bad self" wording is for an error on slot 1 itself, which the
 *        thunk's luaL_checkudata() has already rejected by the time any of this runs.
 * @param expected the type the caller wanted, e.g. "string".
 * @param function the name the method is exported under, e.g. "add".
 *
 * `function` is passed in rather than recovered from the debug API: it is known statically at
 * every call site, and it cannot be derived from the C++ method name -- luaConnectionDynamic-
 * Metadata is exported as "dynamicMetadata", luaPairs as "__pairs".
 */
inline absl::Status argError(lua_State* state, int index, absl::string_view expected,
                             absl::string_view function) {
  ASSERT(index > 1, "stack slot 1 is the method receiver; arguments are read from slot 2 on");
  return absl::InvalidArgumentError(absl::StrCat("bad argument #", index - 1, " to '", function,
                                                 "' (", expected, " expected, got ",
                                                 lua_typename(state, lua_type(state, index)), ")"));
}

/**
 * Read a string argument, applying Lua's implicit number-to-string coercion. Replaces
 * luaL_checkstring()/luaL_checklstring().
 * @return a view of the string, valid while the value remains on the Lua stack.
 */
inline absl::StatusOr<absl::string_view> checkStringOrError(lua_State* state, int index,
                                                            absl::string_view function) {
  // Lua converts between strings and numbers automatically at run time
  // (https://www.lua.org/manual/5.1/manual.html#2.2.1), and luaL_checklstring() honours that, so
  // a number argument is accepted here too. Note the coercion rewrites the stack slot.
  if (lua_isstring(state, index) == 0) {
    return argError(state, index, "string", function);
  }
  size_t length = 0;
  const char* value = lua_tolstring(state, index, &length);
  return absl::string_view(value, length);
}

/**
 * Read an integer argument. Replaces luaL_checkinteger()/luaL_checkint().
 */
inline absl::StatusOr<lua_Integer> checkIntegerOrError(lua_State* state, int index,
                                                       absl::string_view function) {
  if (lua_isnumber(state, index) == 0) {
    return argError(state, index, "number", function);
  }
  return lua_tointeger(state, index);
}

/**
 * Read a number argument. Replaces luaL_checknumber().
 */
inline absl::StatusOr<lua_Number> checkNumberOrError(lua_State* state, int index,
                                                     absl::string_view function) {
  if (lua_isnumber(state, index) == 0) {
    return argError(state, index, "number", function);
  }
  return lua_tonumber(state, index);
}

/**
 * Require an argument of exactly the given Lua type. Replaces luaL_checktype().
 */
inline absl::Status checkTypeOrError(lua_State* state, int index, int type,
                                     absl::string_view function) {
  if (lua_type(state, index) != type) {
    return argError(state, index, lua_typename(state, type), function);
  }
  return absl::OkStatus();
}

/**
 * Read an optional string argument. Replaces luaL_optstring()/luaL_optlstring().
 * @return std::nullopt if the argument is absent or nil, so that callers which care can tell an
 *         omitted argument from an empty string; others can use value_or().
 */
inline absl::StatusOr<std::optional<absl::string_view>>
optStringOrError(lua_State* state, int index, absl::string_view function) {
  if (lua_isnoneornil(state, index)) {
    return std::nullopt;
  }
  return checkStringOrError(state, index, function);
}

/**
 * Non-raising readers for values that are not function arguments.
 *
 * These take a caller-supplied label rather than producing an "bad argument #N" message, for the
 * reads where that would be wrong -- a key or entry of a table being walked with lua_next(), for
 * instance, which is not an argument position at all.
 * @param state the current Lua state.
 * @param index the stack index to read.
 * @param what names the value in the error message, e.g. "header key".
 */

/**
 * Read a Lua string, rejecting a number rather than coercing it. Unlike coercibleStringOrError()
 * this never modifies the slot, so it is the one to use on a key inside a lua_next() traversal:
 * converting a key in place confuses the following lua_next() call.
 * @return a view of the string, valid while the value remains on the Lua stack.
 */
inline absl::StatusOr<absl::string_view> stringOrError(lua_State* state, int index,
                                                       absl::string_view what) {
  const int type = lua_type(state, index);
  if (type != LUA_TSTRING) {
    return absl::InvalidArgumentError(
        absl::StrCat(what, " must be a string, got ", lua_typename(state, type)));
  }
  size_t length = 0;
  const char* value = lua_tolstring(state, index, &length);
  return absl::string_view(value, length);
}

/**
 * Read a Lua string, applying Lua's implicit number-to-string coercion the way checkStringOrError()
 * does. Coercion rewrites the slot, so do not call this on a key inside a lua_next() traversal;
 * use stringOrError() there.
 * @return a view of the string, valid while the value remains on the Lua stack.
 */
inline absl::StatusOr<absl::string_view> coercibleStringOrError(lua_State* state, int index,
                                                                absl::string_view what) {
  if (lua_isstring(state, index) == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat(what, " must be a string, got ", lua_typename(state, lua_type(state, index))));
  }
  size_t length = 0;
  const char* value = lua_tolstring(state, index, &length);
  return absl::string_view(value, length);
}

/**
 * Read a Lua integer, applying the same coercion checkIntegerOrError() does. This never modifies
 * the slot.
 */
inline absl::StatusOr<lua_Integer> integerOrError(lua_State* state, int index,
                                                  absl::string_view what) {
  if (lua_isnumber(state, index) == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat(what, " must be a number, got ", lua_typename(state, lua_type(state, index))));
  }
  return lua_tointeger(state, index);
}

/**
 * Calculate the maximum space needed to be aligned.
 */
template <typename T> constexpr size_t maximumSpaceNeededToAlign() {
  // The allocated memory can be misaligned up to `alignof(T) - 1` bytes. Adding it to the size to
  // allocate.
  return sizeof(T) + alignof(T) - 1;
}

template <typename T> inline T* alignAndCast(void* mem) {
  size_t size = maximumSpaceNeededToAlign<T>();
  return static_cast<T*>(std::align(alignof(T), sizeof(T), mem, size));
}

/**
 * Create a new user data and assign its metatable.
 */
template <typename T> inline T* allocateLuaUserData(lua_State* state) {
  void* mem = lua_newuserdata(state, maximumSpaceNeededToAlign<T>());
  luaL_getmetatable(state, typeid(T).name());
  ASSERT(lua_istable(state, -1));
  lua_setmetatable(state, -2);

  return alignAndCast<T>(mem);
}

class LuaLoggable : public Logger::Loggable<Logger::Id::lua> {
public:
  void scriptLog(spdlog::level::level_enum level, absl::string_view message);
};

/**
 * This is the base class for all C++ objects that we expose out to Lua. The goal is to hide as
 * much ugliness as possible. In general, to use this, do the following:
 * 1) Make your class derive from BaseLuaObject<YourClass>
 * 2) Define methods using DECLARE_LUA_FUNCTION* macros
 * 3) Export your functions by declaring a static exportedFunctions() method in your class.
 * 4) Optionally manage "death" status on your object. (See checkDead() and markDead() below).
 * 5) Generally you will want to hold your objects inside a LuaRef or a LuaDeathRef. See below
 *    for more information on those containers.
 *
 * It's very important to understand the Lua memory model: Once an object is created, *it is
 * owned by Lua*. Lua can GC it at any time. If you want to make sure that does not happen, you
 * must hold a ref to it in C++, generally via LuaRef or LuaDeathRef.
 */
template <class T> class BaseLuaObject : public LuaLoggable {
public:
  using ExportedFunction = std::pair<const char*, lua_CFunction>;
  using ExportedFunctions = std::vector<ExportedFunction>;

  virtual ~BaseLuaObject() = default;

  /**
   * Create a new object of this type, owned by Lua. This type must have previously been registered
   * via the registerType() routine below.
   * @param state supplies the owning Lua state.
   * @param args supplies the variadic constructor arguments for the object.
   * @return a pair containing a pointer to the new object and the state it was created with. (This
   *         is done for convenience when passing a created object to a LuaRef or a LuaDeathRef.
   */
  template <typename... ConstructorArgs>
  static std::pair<T*, lua_State*> create(lua_State* state, ConstructorArgs&&... args) {
    // Memory is allocated via Lua and it is raw. We use placement new to run the constructor.
    T* mem = allocateLuaUserData<T>(state);
    ENVOY_LOG(trace, "creating {} at {}", typeid(T).name(), static_cast<void*>(mem));
    return {new (mem) T(std::forward<ConstructorArgs>(args)...), state};
  }

  /**
   * Register a type with Lua.
   * @param state supplies the state to register with.
   */
  static void registerType(lua_State* state) {
    constexpr std::array log_functions{
        ExportedFunction{"logTrace", static_luaLogTrace},
        ExportedFunction{"logDebug", static_luaLogDebug},
        ExportedFunction{"logInfo", static_luaLogInfo},
        ExportedFunction{"logWarn", static_luaLogWarn},
        ExportedFunction{"logErr", static_luaLogErr},
        ExportedFunction{"logCritical", static_luaLogCritical},
    };

    std::vector<luaL_Reg> to_register;
    // Reserve slots to avoid reallocation, otherwise clang-tidy will complain about it.
    to_register.reserve(log_functions.size() + T::exportedFunctions().size() + 2);

    for (auto& function : log_functions) {
      to_register.push_back({function.first, function.second});
    }

    // Fetch all of the functions to be exported to Lua so that we can register them in the
    // metatable.
    ExportedFunctions functions = T::exportedFunctions();
    for (auto& function : functions) {
      to_register.push_back({function.first, function.second});
    }

    // Always register a __gc method so that we can run the object's destructor. We do this
    // manually because the memory is raw and was allocated by Lua.
    to_register.push_back(
        {"__gc", [](lua_State* state) {
           T* object = alignAndCast<T>(luaL_checkudata(state, 1, typeid(T).name()));
           ENVOY_LOG(trace, "destroying {} at {}", typeid(T).name(), static_cast<void*>(object));
           object->~T();
           return 0;
         }});

    // Add the sentinel.
    to_register.push_back({nullptr, nullptr});

    // Register the type by creating a new metatable, setting __index to itself, and then
    // performing the register.
    ENVOY_LOG(debug, "registering new type: {}", typeid(T).name());
    int rc = luaL_newmetatable(state, typeid(T).name());
    ASSERT(rc == 1);

    lua_pushvalue(state, -1);
    lua_setfield(state, -2, "__index");
    luaL_register(state, nullptr, to_register.data());
  }

  /**
   * This function is called as part of the DECLARE_LUA_FUNCTION* macros. The idea here is that
   * we cannot control when Lua destroys things. However, we may expose wrappers to a script that
   * should not be used after some event. This allows us to mark objects as dead so that if they
   * are used again the caller will raise a Lua error and not reach our code.
   * @return a failed status if the object is dead. The thunk generated by DECLARE_LUA_FUNCTION*
   *         turns it into a Lua error once it has no live C++ objects of its own.
   */
  absl::Status checkDead() const {
    if (dead_) {
      return absl::FailedPreconditionError("object used outside of proper scope");
    }
    return absl::OkStatus();
  }

  /**
   * Mark an object as dead so that a checkDead() call will throw an error. See checkDead().
   */
  void markDead() {
    dead_ = true;
    ENVOY_LOG(trace, "marking dead {} at {}", typeid(T).name(), static_cast<void*>(this));
    onMarkDead();
  }

  /**
   * Mark an object as live so that a checkDead() call will not throw an error. See checkDead().
   */
  void markLive() {
    dead_ = false;
    ENVOY_LOG(trace, "marking live {} at {}", typeid(T).name(), static_cast<void*>(this));
    onMarkLive();
  }

protected:
  /**
   * Called from markDead() when an object is marked dead. This is effectively a C++ destructor for
   * Lua/C objects. Objects can perform inline cleanup or mark other objects as dead if needed. It
   * can also be used to protect objects from use if they get assigned to a global variable and
   * used across coroutines.
   */
  virtual void onMarkDead() {}

  /**
   * Called from markLive() when an object is marked live. This is a companion to onMarkDead(). See
   * the comments there.
   */
  virtual void onMarkLive() {}

private:
  /**
   * Log a message to the Envoy log.
   * @param 1 (string): The log message.
   */
  DECLARE_LUA_FUNCTION(T, luaLogTrace);
  DECLARE_LUA_FUNCTION(T, luaLogDebug);
  DECLARE_LUA_FUNCTION(T, luaLogInfo);
  DECLARE_LUA_FUNCTION(T, luaLogWarn);
  DECLARE_LUA_FUNCTION(T, luaLogErr);
  DECLARE_LUA_FUNCTION(T, luaLogCritical);

  bool dead_{};
};

template <class T> absl::StatusOr<int> BaseLuaObject<T>::luaLogTrace(lua_State* state) {
  const absl::StatusOr<absl::string_view> message =
      Filters::Common::Lua::checkStringOrError(state, 2, "logTrace");
  RETURN_IF_NOT_OK_REF(message.status());
  scriptLog(spdlog::level::trace, *message);
  return 0;
}

template <class T> absl::StatusOr<int> BaseLuaObject<T>::luaLogDebug(lua_State* state) {
  const absl::StatusOr<absl::string_view> message =
      Filters::Common::Lua::checkStringOrError(state, 2, "logDebug");
  RETURN_IF_NOT_OK_REF(message.status());
  scriptLog(spdlog::level::debug, *message);
  return 0;
}

template <class T> absl::StatusOr<int> BaseLuaObject<T>::luaLogInfo(lua_State* state) {
  const absl::StatusOr<absl::string_view> message =
      Filters::Common::Lua::checkStringOrError(state, 2, "logInfo");
  RETURN_IF_NOT_OK_REF(message.status());
  scriptLog(spdlog::level::info, *message);
  return 0;
}

template <class T> absl::StatusOr<int> BaseLuaObject<T>::luaLogWarn(lua_State* state) {
  const absl::StatusOr<absl::string_view> message =
      Filters::Common::Lua::checkStringOrError(state, 2, "logWarn");
  RETURN_IF_NOT_OK_REF(message.status());
  scriptLog(spdlog::level::warn, *message);
  return 0;
}

template <class T> absl::StatusOr<int> BaseLuaObject<T>::luaLogErr(lua_State* state) {
  const absl::StatusOr<absl::string_view> message =
      Filters::Common::Lua::checkStringOrError(state, 2, "logErr");
  RETURN_IF_NOT_OK_REF(message.status());
  scriptLog(spdlog::level::err, *message);
  return 0;
}

template <class T> absl::StatusOr<int> BaseLuaObject<T>::luaLogCritical(lua_State* state) {
  const absl::StatusOr<absl::string_view> message =
      Filters::Common::Lua::checkStringOrError(state, 2, "logCritical");
  RETURN_IF_NOT_OK_REF(message.status());
  scriptLog(spdlog::level::critical, *message);
  return 0;
}

/**
 * This is basically a Lua smart pointer. The idea is that given a Lua object, if we want to
 * guarantee that Lua won't destroy it, we need to reference it. This wraps the reference
 * functionality. While a LuaRef owns an object it's guaranteed that Lua will not GC it.
 * TODO(mattklein123): Add dedicated unit tests. This will require mocking a Lua state.
 */
template <typename T> class LuaRef {
public:
  /**
   * Create an empty LuaRef.
   */
  LuaRef() { reset(); }

  /**
   * Create a LuaRef from an object.
   * @param object supplies the object. Generally this is the return value from a Object::create()
   *        call. The object must be at the top of the Lua stack.
   * @param leave_on_stack supplies whether to leave the object on the stack or not when the ref
   *        is constructed.
   */
  LuaRef(const std::pair<T*, lua_State*>& object, bool leave_on_stack) {
    reset(object, leave_on_stack);
  }

  LuaRef(const LuaRef&) = delete;

  LuaRef(LuaRef&& that) noexcept {
    object_ = that.object_;
    ref_ = that.ref_;
    that.object_ = std::pair<T*, lua_State*>{};
    that.ref_ = LUA_NOREF;
  }

  ~LuaRef() { unref(); }
  T* get() { return object_.first; }

  /**
   * Same as the LuaRef non-default constructor, but post-construction.
   */
  void reset(const std::pair<T*, lua_State*>& object, bool leave_on_stack) {
    unref();

    if (leave_on_stack) {
      lua_pushvalue(object.second, -1);
    }

    object_ = object;
    ref_ = luaL_ref(object_.second, LUA_REGISTRYINDEX);
    ASSERT(ref_ != LUA_REFNIL);
  }

  /**
   * Return a LuaRef to its default/empty state.
   */
  void reset() {
    unref();
    object_ = std::pair<T*, lua_State*>{};
    ref_ = LUA_NOREF;
  }

  /**
   * Push the referenced object back onto the stack.
   */
  void pushStack() {
    ASSERT(object_.first);
    lua_rawgeti(object_.second, LUA_REGISTRYINDEX, ref_);
  }

protected:
  void unref() {
    if (object_.second != nullptr) {
      luaL_unref(object_.second, LUA_REGISTRYINDEX, ref_);
    }
  }

  std::pair<T*, lua_State*> object_;
  int ref_;
};

/**
 * This is a variant of LuaRef which also marks an object as dead during destruction. This is
 * useful if an object should not be used after the scope of the pcall() or resume().
 * TODO(mattklein123): Add dedicated unit tests. This will require mocking a Lua state.
 */
template <typename T> class LuaDeathRef : public LuaRef<T> {
public:
  using LuaRef<T>::LuaRef;

  LuaDeathRef(const LuaDeathRef&) = delete;
  LuaDeathRef(LuaDeathRef&&) noexcept = default;

  ~LuaDeathRef() { markDead(); }

  void markDead() {
    if (this->object_.first) {
      this->object_.first->markDead();
    }
  }

  void markLive() {
    if (this->object_.first) {
      this->object_.first->markLive();
    }
  }

  void reset(const std::pair<T*, lua_State*>& object, bool leave_on_stack) {
    markDead();
    LuaRef<T>::reset(object, leave_on_stack);
  }

  void reset() {
    markDead();
    LuaRef<T>::reset();
  }
};

// Callback invoked when a coroutine yields. It returns a status so an unexpected yield can be
// reported without throwing.
using YieldCallback = std::function<absl::Status()>;

/**
 * This is a wrapper for a Lua coroutine. Lua intermixes coroutine and "thread." Lua does not have
 * real threads, only cooperatively scheduled coroutines.
 */
class Coroutine : Logger::Loggable<Logger::Id::lua> {
public:
  enum class State { NotStarted, Yielded, Finished };

  Coroutine(const std::pair<lua_State*, lua_State*>& new_thread_state);
  lua_State* luaState() { return coroutine_state_.get(); }
  State state() { return state_; }

  /**
   * Start a coroutine.
   * @param function_ref supplies the previously registered function to call. Registered with
   *        ThreadLocalState::registerGlobal().
   * @param num_args supplies the number of arguments to start the coroutine with. They should be
   *        on the stack already.
   * @param yield_callback supplies a callback that will be invoked if the coroutine yields. It
   *        returns a status so an unexpected yield can be reported without throwing.
   * @return the status of the coroutine execution; not OK on a Lua error or a yield_callback error.
   */
  absl::Status start(int function_ref, int num_args, const YieldCallback& yield_callback);

  /**
   * Resume a previously yielded coroutine.
   * @param num_args supplies the number of arguments to resume the coroutine with. They should be
   *        on the stack already.
   * @param yield_callback supplies a callback that will be invoked if the coroutine yields. It
   *        returns a status so an unexpected yield can be reported without throwing.
   * @return the status of the coroutine execution; not OK on a Lua error or a yield_callback error.
   */
  absl::Status resume(int num_args, const YieldCallback& yield_callback);

private:
  LuaRef<lua_State> coroutine_state_;
  State state_{State::NotStarted};
};

using CoroutinePtr = std::unique_ptr<Coroutine>;
using Initializer = std::function<void(lua_State*)>;
using InitializerList = std::vector<Initializer>;

/**
 * Additional module search patterns for a Lua state, prepended to the interpreter's built-in
 * defaults so that a script can require() modules from locations the interpreter does not search
 * on its own. Each member holds patterns already joined in Lua's own ';'-separated syntax, or is
 * empty to leave that search path untouched.
 */
struct PackagePaths {
  // Prepended to `package.path`, for modules that are Lua source.
  std::string path;
  // Prepended to `package.cpath`, for modules that are loadable C libraries.
  std::string cpath;
};

/**
 * This class wraps a Lua state that can be used safely across threads. The model is that every
 * worker gets its own independent state. There is no truly global state that a script can access.
 * This is something that might be provided in the future via an API (not via Lua itself).
 */
class ThreadLocalState : Logger::Loggable<Logger::Id::lua> {
public:
  // creation_status is set (and construction stops early) if the supplied code cannot be parsed.
  // package_paths is applied to every state this object creates, including the one the code is
  // parsed on, so that a require() at the top level of the code resolves the same way there as it
  // will on a worker.
  ThreadLocalState(const std::string& code, const PackagePaths& package_paths,
                   ThreadLocal::SlotAllocator& tls, absl::Status& creation_status);

  /**
   * @return CoroutinePtr a new coroutine.
   */
  CoroutinePtr createCoroutine();

  /**
   * @return a global reference previously registered via registerGlobal(). This may return
   *         LUA_REFNIL if there was no such global.
   * @param slot supplies the global slot/index to lookup.
   */
  int getGlobalRef(uint64_t slot);

  /**
   * Register a global for later use.
   * @param global supplies the name of the global.
   * @param initializers supplies a collection of initializers.
   * @return a slot/index for later use with getGlobalRef().
   */
  uint64_t registerGlobal(const std::string& global, const InitializerList& initializers);

  /**
   * Register a type with the thread local state. After this call the type will be available on
   * all threaded workers.
   */
  template <class T> void registerType() {
    tls_slot_->runOnAllThreads(
        [](OptRef<LuaThreadLocal> tls) { T::registerType(tls->state_.get()); });
  }

  /**
   * Return the number of bytes used by the runtime.
   */
  uint64_t runtimeBytesUsed() {
    uint64_t bytes_used = lua_gc(tlsState().get(), LUA_GCCOUNT, 0) * 1024;
    bytes_used += lua_gc(tlsState().get(), LUA_GCCOUNTB, 0);
    return bytes_used;
  }

  /**
   * Force a full runtime GC.
   */
  void runtimeGC() { lua_gc(tlsState().get(), LUA_GCCOLLECT, 0); }

private:
  struct LuaThreadLocal : public ThreadLocal::ThreadLocalObject {
    LuaThreadLocal(const std::string& code, const PackagePaths& package_paths);

    CSmartPtr<lua_State, lua_close> state_;
    std::vector<int> global_slots_;
  };

  CSmartPtr<lua_State, lua_close>& tlsState() { return (*tls_slot_)->state_; }

  ThreadLocal::TypedSlotPtr<LuaThreadLocal> tls_slot_;
  uint64_t current_global_slot_{};
};

using ThreadLocalStatePtr = std::unique_ptr<ThreadLocalState>;
} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
