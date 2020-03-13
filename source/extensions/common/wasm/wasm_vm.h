#pragma once

#include <memory>

#include "envoy/common/exception.h"
#include "envoy/stats/scope.h"

#include "common/common/logger.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {

class Context;

// Represents a WASM-native word-sized datum. On 32-bit VMs, the high bits are always zero.
// The WASM/VM API treats all bits as significant.
struct Word {
  Word(uint64_t w) : u64_(w) {} // Implicit conversion into Word.
  uint32_t u32() const { return static_cast<uint32_t>(u64_); }
  uint64_t u64_;
};

inline std::ostream& operator<<(std::ostream& os, const Word& w) { return os << w.u64_; }

// Convert Word type for use by 32-bit VMs.
template <typename T> struct ConvertWordTypeToUint32 {
  using type = T; // NOLINT(readability-identifier-naming)
};
template <> struct ConvertWordTypeToUint32<Word> {
  using type = uint32_t; // NOLINT(readability-identifier-naming)
};

// Convert Word-based function types for 32-bit VMs.
template <typename F> struct ConvertFunctionTypeWordToUint32 {};
template <typename R, typename... Args> struct ConvertFunctionTypeWordToUint32<R (*)(Args...)> {
  using type = typename ConvertWordTypeToUint32<R>::type (*)(
      typename ConvertWordTypeToUint32<Args>::type...);
};

template <typename T> inline auto convertWordToUint32(T t) { return t; }
template <> inline auto convertWordToUint32<Word>(Word t) { return static_cast<uint32_t>(t.u64_); }

// Convert a function of the form Word(Word...) to one of the form uint32_t(uint32_t...).
template <typename F, F* fn> struct ConvertFunctionWordToUint32 {
  static void convertFunctionWordToUint32() {}
};
template <typename R, typename... Args, auto (*F)(Args...)->R>
struct ConvertFunctionWordToUint32<R(Args...), F> {
  static typename ConvertWordTypeToUint32<R>::type
  convertFunctionWordToUint32(typename ConvertWordTypeToUint32<Args>::type... args) {
    return convertWordToUint32(F(std::forward<Args>(args)...));
  }
};
template <typename... Args, auto (*F)(Args...)->void>
struct ConvertFunctionWordToUint32<void(Args...), F> {
  static void convertFunctionWordToUint32(typename ConvertWordTypeToUint32<Args>::type... args) {
    F(std::forward<Args>(args)...);
  }
};

#define CONVERT_FUNCTION_WORD_TO_UINT32(_f)                                                        \
  &ConvertFunctionWordToUint32<decltype(_f), _f>::convertFunctionWordToUint32

// These are templates and its helper for constructing signatures of functions calling into and out
// of WASM VMs.
// - WasmFuncTypeHelper is a helper for WasmFuncType and shouldn't be used anywhere else than
// WasmFuncType definition.
// - WasmFuncType takes 4 template parameter which are number of argument, return type, context type
// and param type respectively, resolve to a function type.
//   For example `WasmFuncType<3, void, Context*, Word>` resolves to `void(Context*, Word, Word,
//   Word)`
template <size_t N, class ReturnType, class ContextType, class ParamType,
          class FuncBase = ReturnType(ContextType)>
struct WasmFuncTypeHelper {};

template <size_t N, class ReturnType, class ContextType, class ParamType, class... Args>
struct WasmFuncTypeHelper<N, ReturnType, ContextType, ParamType, ReturnType(ContextType, Args...)> {
  // NOLINTNEXTLINE(readability-identifier-naming)
  using type = typename WasmFuncTypeHelper<N - 1, ReturnType, ContextType, ParamType,
                                           ReturnType(ContextType, Args..., ParamType)>::type;
};

template <class ReturnType, class ContextType, class ParamType, class... Args>
struct WasmFuncTypeHelper<0, ReturnType, ContextType, ParamType, ReturnType(ContextType, Args...)> {
  using type = ReturnType(ContextType, Args...); // NOLINT(readability-identifier-naming)
};

template <size_t N, class ReturnType, class ContextType, class ParamType>
using WasmFuncType = typename WasmFuncTypeHelper<N, ReturnType, ContextType, ParamType>::type;

// Calls into the WASM VM.
// 1st arg is always a pointer to Context (Context*).
template <size_t N> using WasmCallVoid = std::function<WasmFuncType<N, void, Context*, Word>>;
template <size_t N> using WasmCallWord = std::function<WasmFuncType<N, Word, Context*, Word>>;

#define FOR_ALL_WASM_VM_EXPORTS(_f)                                                                \
  _f(WasmCallVoid<0>) _f(WasmCallVoid<1>) _f(WasmCallVoid<2>) _f(WasmCallVoid<3>)                  \
      _f(WasmCallVoid<5>) _f(WasmCallWord<1>) _f(WasmCallWord<2>) _f(WasmCallWord<3>)

// Calls out of the WASM VM.
// 1st arg is always a pointer to raw_context (void*).
template <size_t N> using WasmCallbackVoid = WasmFuncType<N, void, void*, Word>*;
template <size_t N> using WasmCallbackWord = WasmFuncType<N, Word, void*, Word>*;

// Using the standard g++/clang mangling algorithm:
// https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-builtin
// Extended with W = Word
// Z = void, j = uint32_t, l = int64_t, m = uint64_t
using WasmCallback_WWl = Word (*)(void*, Word, int64_t);
using WasmCallback_WWlWW = Word (*)(void*, Word, int64_t, Word, Word);
using WasmCallback_WWm = Word (*)(void*, Word, uint64_t);
using WasmCallback_dd = double (*)(void*, double);

#define FOR_ALL_WASM_VM_IMPORTS(_f)                                                                \
  _f(WasmCallbackVoid<0>) _f(WasmCallbackVoid<1>) _f(WasmCallbackVoid<2>) _f(WasmCallbackVoid<3>)  \
      _f(WasmCallbackVoid<4>) _f(WasmCallbackWord<0>) _f(WasmCallbackWord<1>)                      \
          _f(WasmCallbackWord<2>) _f(WasmCallbackWord<3>) _f(WasmCallbackWord<4>)                  \
              _f(WasmCallbackWord<5>) _f(WasmCallbackWord<6>) _f(WasmCallbackWord<7>)              \
                  _f(WasmCallbackWord<8>) _f(WasmCallbackWord<9>) _f(WasmCallbackWord<10>)         \
                      _f(WasmCallback_WWl) _f(WasmCallback_WWlWW) _f(WasmCallback_WWm)             \
                          _f(WasmCallback_dd)

enum class Cloneable {
  NotCloneable,      // VMs can not be cloned and should be created from scratch.
  CompiledBytecode,  // VMs can be cloned with compiled bytecode.
  InstantiatedModule // VMs can be cloned from an instantiated module.
};

// Wasm VM instance. Provides the low level WASM interface.
class WasmVm : public Logger::Loggable<Logger::Id::wasm> {
public:
  using WasmVmPtr = std::unique_ptr<WasmVm>;

  virtual ~WasmVm() = default;
  /**
   * Return the runtime identifier.
   * @return one of WasmRuntimeValues from well_known_names.h (e.g. "envoy.wasm.runtime.null").
   */
  virtual absl::string_view runtime() PURE;

  /**
   * Whether or not the VM implementation supports cloning. Cloning is VM system dependent.
   * When a VM is configured a single VM is instantiated to check that the .wasm file is valid and
   * to do VM system specific initialization. In the case of WAVM this is potentially ahead-of-time
   * compilation. Then, if cloning is supported, we clone that VM for each worker, potentially
   * copying and sharing the initialized data structures for efficiency. Otherwise we create an new
   * VM from scratch for each worker.
   * @return one of enum Cloneable with the VMs cloneability.
   */
  virtual Cloneable cloneable() PURE;

  /**
   * Make a worker/thread-specific copy if supported by the underlying VM system (see cloneable()
   * above). If not supported, the caller will need to create a new VM from scratch. If supported,
   * the clone may share compiled code and other read-only data with the source VM.
   * @return a clone of 'this' (e.g. for a different worker/thread).
   */
  virtual WasmVmPtr clone() PURE;

  /**
   * Load the WASM code from a file. Return true on success. Once the module is loaded it can be
   * queried, e.g. to see which version of emscripten support is required. After loading, the
   * appropriate ABI callbacks can be registered and then the module can be link()ed (see below).
   * @param code the WASM binary code (or registered NullVm plugin name).
   * @param allow_precompiled if true, allows supporting VMs (e.g. WAVM) to load the binary
   * machine code from a user-defined section of the WASM file. Because that code is not verified by
   * the envoy process it is up to the user to ensure that the code is both safe and is built for
   * the linked in version of WAVM.
   * @return whether or not the load was successful.
   */
  virtual bool load(const std::string& code, bool allow_precompiled) PURE;

  /**
   * Link the WASM code to the host-provided functions, e.g. the ABI. Prior to linking, the module
   * should be loaded and the ABI callbacks registered (see above). Linking should be done once
   * after load().
   * @param debug_name user-provided name for use in log and error messages.
   */
  virtual void link(absl::string_view debug_name) PURE;

  /**
   * Get size of the currently allocated memory in the VM.
   * @return the size of memory in bytes.
   */
  virtual uint64_t getMemorySize() PURE;

  /**
   * Convert a block of memory in the VM to a string_view.
   * @param pointer the offset into VM memory of the requested VM memory block.
   * @param size the size of the requested VM memory block.
   * @return if std::nullopt then the pointer/size pair were invalid, otherwise returns
   * a host string_view pointing to the pointer/size pair in VM memory.
   */
  virtual absl::optional<absl::string_view> getMemory(uint64_t pointer, uint64_t size) PURE;

  /**
   * Set a block of memory in the VM, returns true on success, false if the pointer/size is invalid.
   * @param pointer the offset into VM memory describing the start of a region of VM memory.
   * @param size the size of the region of VM memory.
   * @return whether or not the pointer/size pair was a valid VM memory block.
   */
  virtual bool setMemory(uint64_t pointer, uint64_t size, const void* data) PURE;

  /**
   * Get a VM native Word (e.g. sizeof(void*) or sizeof(size_t)) from VM memory, returns true on
   * success, false if the pointer is invalid. WASM-32 VMs have 32-bit native words and WASM-64 VMs
   * (not yet supported) will have 64-bit words as does the Null VM (compiled into 64-bit Envoy).
   * This function can be used to chase pointers in VM memory.
   * @param pointer the offset into VM memory describing the start of VM native word size block.
   * @param data a pointer to a Word whose contents will be filled from the VM native word at
   * 'pointer'.
   * @return whether or not the pointer was to a valid VM memory block of VM native word size.
   */
  virtual bool getWord(uint64_t pointer, Word* data) PURE;

  /**
   * Set a Word in the VM, returns true on success, false if the pointer is invalid.
   * See getWord above for details. This function can be used (for example) to set indirect pointer
   * return values (e.g. proxy_getHeaderHapValue(... const char** value_ptr, size_t* value_size).
   * @param pointer the offset into VM memory describing the start of VM native word size block.
   * @param data a Word whose contents will be written in VM native word size at 'pointer'.
   * @return whether or not the pointer was to a valid VM memory block of VM native word size.
   */
  virtual bool setWord(uint64_t pointer, Word data) PURE;

  /**
   * Get the contents of the custom section with the given name or "" if it does not exist.
   * @param name the name of the custom section to get.
   * @return the contents of the custom section (if any). The result will be empty if there
   * is no such section.
   */
  virtual absl::string_view getCustomSection(absl::string_view name) PURE;

  /**
   * Get the name of the custom section that contains precompiled module.
   * @return the name of the custom section that contains precompiled module.
   */
  virtual absl::string_view getPrecompiledSectionName() PURE;

  /**
   * Get typed function exported by the WASM module.
   */
#define _GET_FUNCTION(_T) virtual void getFunction(absl::string_view function_name, _T* f) PURE;
  FOR_ALL_WASM_VM_EXPORTS(_GET_FUNCTION)
#undef _GET_FUNCTION

  /**
   * Register typed callbacks exported by the host environment.
   */
#define _REGISTER_CALLBACK(_T)                                                                     \
  virtual void registerCallback(absl::string_view moduleName, absl::string_view function_name,     \
                                _T f, typename ConvertFunctionTypeWordToUint32<_T>::type) PURE;
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_CALLBACK)
#undef _REGISTER_CALLBACK
};
using WasmVmPtr = std::unique_ptr<WasmVm>;

// Exceptions for issues with the WasmVm.
class WasmVmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

// Exceptions for issues with the WebAssembly code.
class WasmException : public EnvoyException {
public:
  using EnvoyException::EnvoyException;
};

// Thread local state set during a call into a WASM VM so that calls coming out of the
// VM can be attributed correctly to calling Filter. We use thread_local instead of ThreadLocal
// because this state is live only during the calls and does not need to be initialized consistently
// over all workers as with ThreadLocal data.
extern thread_local Envoy::Extensions::Common::Wasm::Context* current_context_;

// Requested effective context set by code within the VM to request that the calls coming out of the
// VM be attributed to another filter, for example if a control plane gRPC comes back to the
// RootContext which effects some set of waiting filters.
extern thread_local uint32_t effective_context_id_;

// Helper to save and restore thread local VM call context information to support reentrant calls.
// NB: this happens for example when a call from the VM invokes a handler which needs to _malloc
// memory in the VM.
struct SaveRestoreContext {
  explicit SaveRestoreContext(Context* context) {
    saved_context = current_context_;
    saved_effective_context_id_ = effective_context_id_;
    current_context_ = context;
    effective_context_id_ = 0; // No effective context id.
  }
  ~SaveRestoreContext() {
    current_context_ = saved_context;
    effective_context_id_ = saved_effective_context_id_;
  }
  Context* saved_context;
  uint32_t saved_effective_context_id_;
};

// Create a new low-level WASM VM using runtime of the given type (e.g. "envoy.wasm.runtime.wavm").
WasmVmPtr createWasmVm(absl::string_view runtime, const Stats::ScopeSharedPtr& scope);

} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
