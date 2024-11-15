#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/exception.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/assert.h"
#include "source/common/common/c_smart_ptr.h"
#include "source/common/common/logger.h"

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
 * Base macro for declaring a Lua/C function. Any function declared will need to be exported via
 * the exportedFunctions() function in BaseLuaObject. See BaseLuaObject below for more
 * information. This macro declares a static "thunk" which checks the user data, optionally checks
 * for object death (again see BaseLuaObject below for more info), and then invokes a normal
 * object method. The actual object method needs to be implemented by the class.
 * @param Class supplies the owning class name.
 * @param Name supplies the function name.
 * @param Index supplies the stack index where "this" (Lua/C userdata) is found.
 */
#define DECLARE_LUA_FUNCTION_EX(Class, Name, Index)                                                \
  static int static_##Name(lua_State* state) {                                                     \
    Class* object = ::Envoy::Extensions::Filters::Common::Lua::alignAndCast<Class>(                \
        luaL_checkudata(state, Index, typeid(Class).name()));                                      \
    object->checkDead(state);                                                                      \
    return object->Name(state);                                                                    \
  }                                                                                                \
  int Name(lua_State* state);

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
 * Get absl::string_view from Lua string. This checks if the argument at index is a string
 * and build an absl::string_view from it.
 * @param state the current Lua state.
 * @param index the index of argument.
 * @return absl::string_view of Lua string with proper string length.
 **/
inline absl::string_view getStringViewFromLuaString(lua_State* state, int index) {
  size_t input_size = 0;
  // When the argument at index in Lua state is not a string, for example, giving a table to
  // logTrace (which uses this function under the hood), Lua script exits with an error like the
  // following: "[string \"...\"]:3: bad argument #1 to 'logTrace' (string expected, got table)".
  // However,`luaL_checklstring` accepts a number as its argument and implicitly converts it to a
  // string, since Lua provides automatic conversion between string and number values at run time
  // (https://www.lua.org/manual/5.1/manual.html#2.2.1).
  const char* input = luaL_checklstring(state, index, &input_size);
  return {input, input_size};
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
template <class T> class BaseLuaObject : protected Logger::Loggable<Logger::Id::lua> {
public:
  using ExportedFunctions = std::vector<std::pair<const char*, lua_CFunction>>;

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
    std::vector<luaL_Reg> to_register;

    // Fetch all of the functions to be exported to Lua so that we can register them in the
    // metatable.
    ExportedFunctions functions = T::exportedFunctions();
    for (auto function : functions) {
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
   * are used again they will throw a Lua error and not reach our code.
   * @param state supplies the calling LuaState.
   */
  int checkDead(lua_State* state) {
    if (dead_) {
      return luaL_error(state, "object used outside of proper scope");
    }
    return 0;
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
  bool dead_{};
};

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
   * @param yield_callback supplies a callback that will be invoked if the coroutine yields.
   */
  void start(int function_ref, int num_args, const std::function<void()>& yield_callback);

  /**
   * Resume a previously yielded coroutine.
   * @param num_args supplies the number of arguments to resume the coroutine with. They should be
   *        on the stack already.
   * @param yield_callback supplies a callback that will be invoked if the coroutine yields.
   */
  void resume(int num_args, const std::function<void()>& yield_callback);

private:
  LuaRef<lua_State> coroutine_state_;
  State state_{State::NotStarted};
};

using CoroutinePtr = std::unique_ptr<Coroutine>;
using Initializer = std::function<void(lua_State*)>;
using InitializerList = std::vector<Initializer>;

/**
 * This class wraps a Lua state that can be used safely across threads. The model is that every
 * worker gets its own independent state. There is no truly global state that a script can access.
 * This is something that might be provided in the future via an API (not via Lua itself).
 */
class ThreadLocalState : Logger::Loggable<Logger::Id::lua> {
public:
  ThreadLocalState(const std::string& code, ThreadLocal::SlotAllocator& tls);

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
    LuaThreadLocal(const std::string& code);

    CSmartPtr<lua_State, lua_close> state_;
    std::vector<int> global_slots_;
  };

  CSmartPtr<lua_State, lua_close>& tlsState() { return (*tls_slot_)->state_; }

  ThreadLocal::TypedSlotPtr<LuaThreadLocal> tls_slot_;
  uint64_t current_global_slot_{};
};

using ThreadLocalStatePtr = std::unique_ptr<ThreadLocalState>;

/**
 * An exception specific to Lua errors.
 */
class LuaException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};
} // namespace Lua
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
