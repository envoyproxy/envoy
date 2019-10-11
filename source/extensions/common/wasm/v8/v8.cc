#include "extensions/common/wasm/v8/v8.h"

#include <memory>
#include <utility>
#include <vector>

#include "common/common/assert.h"

#include "extensions/common/wasm/well_known_names.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "wasm-api/wasm.hh"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace V8 {

wasm::Engine* engine() {
  static const auto engine = wasm::Engine::make();
  return engine.get();
}

struct FuncData {
  FuncData(std::string name) : name(std::move(name)) {}

  std::string name;
  wasm::own<wasm::Func> callback;
  void* raw_func;
};

using FuncDataPtr = std::unique_ptr<FuncData>;

class V8 : public WasmVm {
public:
  V8() = default;

  // Extensions::Common::Wasm::WasmVm
  absl::string_view runtime() override { return WasmRuntimeNames::get().V8; }

  bool load(const std::string& code, bool allow_precompiled) override;
  absl::string_view getUserSection(absl::string_view name) override;
  void link(absl::string_view debug_name, bool needs_emscripten) override;
  void setMemoryLayout(uint64_t stack_base, uint64_t heap_base,
                       uint64_t heap_base_pointer) override;

  // We don't care about this.
  void makeModule(absl::string_view) override {}

  // V8 is currently not cloneable.
  bool cloneable() override { return false; }
  std::unique_ptr<WasmVm> clone() override { return nullptr; }

  void start(Context* context) override;

  uint64_t getMemorySize() override;
  absl::optional<absl::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool getMemoryOffset(void* host_pointer, uint64_t* vm_pointer) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void* data) override;
  bool getWord(uint64_t pointer, Word* word) override;
  bool setWord(uint64_t pointer, Word word) override;

#define _REGISTER_HOST_GLOBAL(T)                                                                   \
  std::unique_ptr<Global<T>> makeGlobal(absl::string_view module_name, absl::string_view name,     \
                                        T initial_value) override {                                \
    return registerHostGlobalImpl(module_name, name, initial_value);                               \
  };
  _REGISTER_HOST_GLOBAL(Word);
  _REGISTER_HOST_GLOBAL(double);
#undef _REGISTER_HOST_GLOBAL

#define _REGISTER_HOST_FUNCTION(T)                                                                 \
  void registerCallback(absl::string_view module_name, absl::string_view function_name, T,         \
                        typename ConvertFunctionTypeWordToUint32<T>::type f) override {            \
    registerHostFunctionImpl(module_name, function_name, f);                                       \
  };
  FOR_ALL_WASM_VM_IMPORTS(_REGISTER_HOST_FUNCTION)
#undef _REGISTER_HOST_FUNCTION

#define _GET_MODULE_FUNCTION(T)                                                                    \
  void getFunction(absl::string_view function_name, T* f) override {                               \
    getModuleFunctionImpl(function_name, f);                                                       \
  };
  FOR_ALL_WASM_VM_EXPORTS(_GET_MODULE_FUNCTION)
#undef _GET_MODULE_FUNCTION

private:
  void callModuleFunction(Context* context, absl::string_view function_name, const wasm::Val args[],
                          wasm::Val results[]);
  void callModuleFunction(Context* context, absl::string_view function_name, const wasm::Func* func,
                          const wasm::Val args[], wasm::Val results[]);

  template <typename T>
  std::unique_ptr<Global<T>> registerHostGlobalImpl(absl::string_view module_name,
                                                    absl::string_view name, T initial_value);

  template <typename... Args>
  void registerHostFunctionImpl(absl::string_view module_name, absl::string_view function_name,
                                void (*function)(void*, Args...));

  template <typename R, typename... Args>
  void registerHostFunctionImpl(absl::string_view module_name, absl::string_view function_name,
                                R (*function)(void*, Args...));

  template <typename... Args>
  void getModuleFunctionImpl(absl::string_view function_name,
                             std::function<void(Context*, Args...)>* function);

  template <typename R, typename... Args>
  void getModuleFunctionImpl(absl::string_view function_name,
                             std::function<R(Context*, Args...)>* function);

  wasm::vec<byte_t> source_ = wasm::vec<byte_t>::invalid();
  wasm::own<wasm::Store> store_;
  wasm::own<wasm::Module> module_;
  wasm::own<wasm::Instance> instance_;
  wasm::own<wasm::Memory> memory_;
  wasm::own<wasm::Table> table_;

  absl::flat_hash_map<std::string, wasm::own<wasm::Global>> host_globals_;
  absl::flat_hash_map<std::string, FuncDataPtr> host_functions_;
  absl::flat_hash_map<std::string, wasm::own<wasm::Func>> module_functions_;

  uint32_t memory_stack_base_;
  uint32_t memory_heap_base_;
  uint32_t memory_heap_base_pointer_;

  bool module_needs_emscripten_{};
};

// Helper functions.

static std::string printValue(const wasm::Val& value) {
  switch (value.kind()) {
  case wasm::I32:
    return std::to_string(value.get<uint32_t>());
  case wasm::I64:
    return std::to_string(value.get<uint64_t>());
  case wasm::F32:
    return std::to_string(value.get<float>());
  case wasm::F64:
    return std::to_string(value.get<double>());
  default:
    return "unknown";
  }
}

static std::string printValues(const wasm::Val values[], size_t size) {
  if (size == 0) {
    return "";
  }

  std::string s;
  for (size_t i = 0; i < size; i++) {
    if (i) {
      s.append(", ");
    }
    s.append(printValue(values[i]));
  }
  return s;
}

static const char* printValKind(wasm::ValKind kind) {
  switch (kind) {
  case wasm::I32:
    return "i32";
  case wasm::I64:
    return "i64";
  case wasm::F32:
    return "f32";
  case wasm::F64:
    return "f64";
  case wasm::ANYREF:
    return "anyref";
  case wasm::FUNCREF:
    return "funcref";
  default:
    return "unknown";
  }
}

static std::string printValTypes(const wasm::ownvec<wasm::ValType>& types) {
  if (types.size() == 0) {
    return "void";
  }

  std::string s;
  s.reserve(types.size() * 8 /* max size + " " */ - 1);
  for (size_t i = 0; i < types.size(); i++) {
    if (i) {
      s.append(" ");
    }
    s.append(printValKind(types[i]->kind()));
  }
  return s;
}

static bool equalValTypes(const wasm::ownvec<wasm::ValType>& left,
                          const wasm::ownvec<wasm::ValType>& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); i++) {
    if (left[i]->kind() != right[i]->kind()) {
      return false;
    }
  }
  return true;
}

static uint32_t parseVarint(const byte_t*& pos, const byte_t* end) {
  uint32_t n = 0;
  uint32_t shift = 0;
  byte_t b;
  do {
    if (pos + 1 > end) {
      throw WasmVmException("Failed to parse corrupted WASM module");
    }
    b = *pos++;
    n += (b & 0x7f) << shift;
    shift += 7;
  } while ((b & 0x80) != 0);
  return n;
}

// Template magic.

template <typename T> struct ConvertWordType {
  using type = T; // NOLINT(readability-identifier-naming)
};
template <> struct ConvertWordType<Word> {
  using type = uint32_t; // NOLINT(readability-identifier-naming)
};

template <typename T> wasm::Val makeVal(T t) { return wasm::Val::make(t); }
template <> wasm::Val makeVal(Word t) { return wasm::Val::make(static_cast<uint32_t>(t.u64_)); }

template <typename T> constexpr auto convertArgToValKind();
template <> constexpr auto convertArgToValKind<Word>() { return wasm::I32; };
template <> constexpr auto convertArgToValKind<int32_t>() { return wasm::I32; };
template <> constexpr auto convertArgToValKind<uint32_t>() { return wasm::I32; };
template <> constexpr auto convertArgToValKind<int64_t>() { return wasm::I64; };
template <> constexpr auto convertArgToValKind<uint64_t>() { return wasm::I64; };
template <> constexpr auto convertArgToValKind<float>() { return wasm::F32; };
template <> constexpr auto convertArgToValKind<double>() { return wasm::F64; };

template <typename T> struct V8ProxyForGlobal : Global<T> {
  V8ProxyForGlobal(wasm::Global* value) : global_(value) {}

  T get() override { return global_->get().get<typename ConvertWordTypeToUint32<T>::type>(); };
  void set(const T& value) override { global_->set(makeVal(static_cast<T>(value))); };

  wasm::Global* global_;
};

template <typename T, std::size_t... I>
constexpr auto convertArgsTupleToValTypesImpl(absl::index_sequence<I...>) {
  return wasm::ownvec<wasm::ValType>::make(
      wasm::ValType::make(convertArgToValKind<typename std::tuple_element<I, T>::type>())...);
}

template <typename T> constexpr auto convertArgsTupleToValTypes() {
  return convertArgsTupleToValTypesImpl<T>(absl::make_index_sequence<std::tuple_size<T>::value>());
}

template <typename T, typename U, std::size_t... I>
constexpr T convertValTypesToArgsTupleImpl(const U& arr, absl::index_sequence<I...>) {
  return std::make_tuple(
      (arr[I]
           .template get<
               typename ConvertWordType<typename std::tuple_element<I, T>::type>::type>())...);
}

template <typename T, typename U> constexpr T convertValTypesToArgsTuple(const U& arr) {
  return convertValTypesToArgsTupleImpl<T>(arr,
                                           absl::make_index_sequence<std::tuple_size<T>::value>());
}

// V8 implementation.

bool V8::load(const std::string& code, bool /* allow_precompiled */) {
  ENVOY_LOG(trace, "[wasm] load()");
  store_ = wasm::Store::make(engine());
  RELEASE_ASSERT(store_ != nullptr, "");

  source_ = wasm::vec<byte_t>::make_uninitialized(code.size());
  ::memcpy(source_.get(), code.data(), code.size());

  module_ = wasm::Module::make(store_.get(), source_);
  return module_ != nullptr;
}

absl::string_view V8::getUserSection(absl::string_view name) {
  ENVOY_LOG(trace, "[wasm] getUserSection(\"{}\")", name);
  ASSERT(source_.get() != nullptr);

  const byte_t* end = source_.get() + source_.size();
  const byte_t* pos = source_.get() + 8; // skip header
  while (pos < end) {
    if (pos + 1 > end) {
      throw WasmVmException("Failed to parse corrupted WASM module");
    }
    auto type = *pos++;
    auto rest = parseVarint(pos, end);
    if (pos + rest > end) {
      throw WasmVmException("Failed to parse corrupted WASM module");
    }
    if (type == 0 /* custom section */) {
      auto start = pos;
      auto len = parseVarint(pos, end);
      if (pos + len > end) {
        throw WasmVmException("Failed to parse corrupted WASM module");
      }
      pos += len;
      rest -= (pos - start);
      if (len == name.size() && ::memcmp(pos - len, name.data(), len) == 0) {
        ENVOY_LOG(trace, "[wasm] getUserSection(\"{}\") found, size: {}", name, rest);
        return {pos, rest};
      }
    }
    pos += rest;
  }
  return "";
}

void V8::link(absl::string_view debug_name, bool needs_emscripten) {
  ENVOY_LOG(trace, "[wasm] link(\"{}\"), emscripten: {}", debug_name, needs_emscripten);
  ASSERT(module_ != nullptr);

  const auto import_types = module_.get()->imports();
  std::vector<const wasm::Extern*> imports;

  for (size_t i = 0; i < import_types.size(); i++) {
    absl::string_view module(import_types[i]->module().get(), import_types[i]->module().size());
    absl::string_view name(import_types[i]->name().get(), import_types[i]->name().size());
    auto import_type = import_types[i]->type();

    switch (import_type->kind()) {

    case wasm::EXTERN_FUNC: {
      ENVOY_LOG(trace, "[wasm] link(), export host func: {}.{} ({} -> {})", module, name,
                printValTypes(import_type->func()->params()),
                printValTypes(import_type->func()->results()));

      const wasm::Func* func = nullptr;
      auto it = host_functions_.find(absl::StrCat(module, ".", name));
      if (it != host_functions_.end()) {
        func = it->second->callback.get();
      } else {
        it = host_functions_.find(absl::StrCat("envoy", ".", name));
        if (it != host_functions_.end()) {
          func = it->second->callback.get();
        }
      }
      if (func) {
        if (equalValTypes(import_type->func()->params(), func->type()->params()) &&
            equalValTypes(import_type->func()->results(), func->type()->results())) {
          imports.push_back(func);
        } else {
          throw WasmVmException(fmt::format(
              "Failed to load WASM module due to an import type mismatch: {}.{}, "
              "want: {} -> {}, but host exports: {} -> {}",
              module, name, printValTypes(import_type->func()->params()),
              printValTypes(import_type->func()->results()), printValTypes(func->type()->params()),
              printValTypes(func->type()->results())));
        }
      } else {
        throw WasmVmException(
            fmt::format("Failed to load WASM module due to a missing import: {}.{}", module, name));
      }
    } break;

    case wasm::EXTERN_GLOBAL: {
      ENVOY_LOG(trace, "[wasm] link(), export host global: {}.{} ({})", module, name,
                printValKind(import_type->global()->content()->kind()));

      const wasm::Global* global = nullptr;
      auto it = host_globals_.find(absl::StrCat(module, ".", name));
      if (it != host_globals_.end()) {
        global = it->second.get();
      } else {
        it = host_globals_.find(absl::StrCat("envoy", ".", name));
        if (it != host_globals_.end()) {
          global = it->second.get();
        }
      }
      if (global) {
        imports.push_back(global);
      } else {
        throw WasmVmException(
            fmt::format("Failed to load WASM module due to a missing import: {}.{}", module, name));
      }
    } break;

    case wasm::EXTERN_MEMORY: {
      ENVOY_LOG(trace, "[wasm] link(), export host memory: {}.{} (min: {} max: {})", module, name,
                import_type->memory()->limits().min, import_type->memory()->limits().max);

      ASSERT(memory_ == nullptr);
      auto type = wasm::MemoryType::make(import_type->memory()->limits());
      memory_ = wasm::Memory::make(store_.get(), type.get());
      imports.push_back(memory_.get());
    } break;

    case wasm::EXTERN_TABLE: {
      ENVOY_LOG(trace, "[wasm] link(), export host table: {}.{} (min: {} max: {})", module, name,
                import_type->table()->limits().min, import_type->table()->limits().max);

      ASSERT(table_ == nullptr);
      auto type =
          wasm::TableType::make(wasm::ValType::make(import_type->table()->element()->kind()),
                                import_type->table()->limits());
      table_ = wasm::Table::make(store_.get(), type.get());
      imports.push_back(table_.get());
    } break;
    }
  }

  ASSERT(import_types.size() == imports.size());

  instance_ = wasm::Instance::make(store_.get(), module_.get(), imports.data());
  RELEASE_ASSERT(instance_ != nullptr, "");
  module_needs_emscripten_ = needs_emscripten;

  const auto export_types = module_.get()->exports();
  const auto exports = instance_.get()->exports();
  ASSERT(export_types.size() == exports.size());

  for (size_t i = 0; i < export_types.size(); i++) {
    absl::string_view name(export_types[i]->name().get(), export_types[i]->name().size());
    auto export_type = export_types[i]->type();
    auto export_item = exports[i].get();
    ASSERT(export_type->kind() == export_item->kind());

    switch (export_type->kind()) {

    case wasm::EXTERN_FUNC: {
      ENVOY_LOG(trace, "[wasm] link(), import module func: {} ({} -> {})", name,
                printValTypes(export_type->func()->params()),
                printValTypes(export_type->func()->results()));

      ASSERT(export_item->func() != nullptr);
      module_functions_.emplace(name, export_item->func()->copy());
    } break;

    case wasm::EXTERN_GLOBAL: {
      // TODO(PiotrSikora): add support when/if needed.
      ENVOY_LOG(trace, "[wasm] link(), import module global: {} ({}) --- IGNORED", name,
                printValKind(export_type->global()->content()->kind()));
    } break;

    case wasm::EXTERN_MEMORY: {
      ENVOY_LOG(trace, "[wasm] link(), import module memory: {} (min: {} max: {})", name,
                export_type->memory()->limits().min, export_type->memory()->limits().max);

      ASSERT(export_item->memory() != nullptr);
      ASSERT(memory_ == nullptr);
      memory_ = exports[i]->memory()->copy();
    } break;

    case wasm::EXTERN_TABLE: {
      // TODO(PiotrSikora): add support when/if needed.
      ENVOY_LOG(trace, "[wasm] link(), import module table: {} (min: {} max: {}) --- IGNORED", name,
                export_type->table()->limits().min, export_type->table()->limits().max);
    } break;
    }
  }
}

void V8::setMemoryLayout(uint64_t stack_base, uint64_t heap_base, uint64_t heap_base_pointer) {
  ENVOY_LOG(trace, "[wasm] setMemoryLayout({}, {}, {})", stack_base, heap_base, heap_base_pointer);

  memory_stack_base_ = stack_base;
  memory_heap_base_ = heap_base;
  memory_heap_base_pointer_ = heap_base_pointer;
}

void V8::start(Context* context) {
  ENVOY_LOG(trace, "[wasm] start()");

  if (module_needs_emscripten_) {
    if (memory_stack_base_) {
      // Workaround for Emscripten versions without heap (dynamic) base in metadata.
      const wasm::Val args[] = {wasm::Val::make(memory_stack_base_),
                                wasm::Val::make(memory_heap_base_)};
      callModuleFunction(context, "establishStackSpace", args, nullptr);
    }

    // Set initial heap base value at DYNAMICTOP_PTR.
    setMemory(memory_heap_base_pointer_, sizeof(uint32_t), &memory_heap_base_);

    callModuleFunction(context, "globalCtors", nullptr, nullptr);

    for (const auto& kv : module_functions_) {
      if (absl::StartsWith(kv.first, "__GLOBAL__")) {
        callModuleFunction(context, kv.first, kv.second.get(), nullptr, nullptr);
      }
    }
  }

  callModuleFunction(context, "__post_instantiate", nullptr, nullptr);
}

void V8::callModuleFunction(Context* context, absl::string_view function_name,
                            const wasm::Val args[], wasm::Val results[]) {
  auto it = module_functions_.find(function_name);
  if (it != module_functions_.end()) {
    callModuleFunction(context, function_name, it->second.get(), args, results);
  }
}

void V8::callModuleFunction(Context* context, absl::string_view function_name,
                            const wasm::Func* func, const wasm::Val args[], wasm::Val results[]) {
  // TODO(PiotrSikora): print params when/if needed (all relevant callers are void(void)).
  ENVOY_LOG(trace, "[wasm] [host->vm] {}({})", function_name, args ? "???" : "");

  SaveRestoreContext _saved_context(context);
  auto trap = func->call(args, results);
  if (trap) {
    throw WasmVmException(
        fmt::format("Function: {} failed: {}", function_name,
                    absl::string_view(trap->message().get(), trap->message().size())));
  }

  // TODO(PiotrSikora): print return values when/if needed (all relevant callers are void(void)).
  ENVOY_LOG(trace, "[wasm] [host<-vm] {} return: {}", function_name, results ? "???" : "void");
}

uint64_t V8::getMemorySize() {
  ENVOY_LOG(trace, "[wasm] getMemorySize()");
  return memory_->data_size();
}

absl::optional<absl::string_view> V8::getMemory(uint64_t pointer, uint64_t size) {
  ENVOY_LOG(trace, "[wasm] getMemory({}, {})", pointer, size);
  ASSERT(memory_ != nullptr);
  if (pointer + size > memory_->data_size()) {
    return absl::nullopt;
  }
  return absl::string_view(memory_->data() + pointer, size);
}

bool V8::getMemoryOffset(void* host_pointer, uint64_t* vm_pointer) {
  ENVOY_LOG(trace, "[wasm] getMemoryOffset({})", host_pointer);
  ASSERT(memory_ != nullptr);
  if (static_cast<char*>(host_pointer) >= memory_->data() ||
      static_cast<char*>(host_pointer) <= memory_->data() + memory_->data_size()) {
    return false;
  }
  *vm_pointer = static_cast<char*>(host_pointer) - memory_->data();
  return true;
}

bool V8::setMemory(uint64_t pointer, uint64_t size, const void* data) {
  ENVOY_LOG(trace, "[wasm] setMemory({}, {})", pointer, size);
  ASSERT(memory_ != nullptr);
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  ::memcpy(memory_->data() + pointer, data, size);
  return true;
}

bool V8::getWord(uint64_t pointer, Word* word) {
  ENVOY_LOG(trace, "[wasm] getWord({})", pointer);
  auto size = sizeof(uint32_t);
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  uint32_t word32;
  ::memcpy(&word32, memory_->data() + pointer, size);
  word->u64_ = word32;
  return true;
}

bool V8::setWord(uint64_t pointer, Word word) {
  ENVOY_LOG(trace, "[wasm] setWord({}, {})", pointer, word.u64_);
  auto size = sizeof(uint32_t);
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  uint32_t word32 = word.u32();
  ::memcpy(memory_->data() + pointer, &word32, size);
  return true;
}

template <typename T>
std::unique_ptr<Global<T>> V8::registerHostGlobalImpl(absl::string_view module_name,
                                                      absl::string_view name, T initial_value) {
  ENVOY_LOG(trace, "[wasm] registerHostGlobal(\"{}.{}\", {})", module_name, name, initial_value);
  auto value = makeVal(initial_value);
  auto type = wasm::GlobalType::make(wasm::ValType::make(value.kind()), wasm::CONST);
  auto global = wasm::Global::make(store_.get(), type.get(), value);
  auto proxy = std::make_unique<V8ProxyForGlobal<T>>(global.get());
  host_globals_.emplace(absl::StrCat(module_name, ".", name), std::move(global));
  return proxy;
}

template <typename... Args>
void V8::registerHostFunctionImpl(absl::string_view module_name, absl::string_view function_name,
                                  void (*function)(void*, Args...)) {
  ENVOY_LOG(trace, "[wasm] registerHostFunction(\"{}.{}\")", module_name, function_name);
  auto data = std::make_unique<FuncData>(absl::StrCat(module_name, ".", function_name));
  auto type = wasm::FuncType::make(convertArgsTupleToValTypes<std::tuple<Args...>>(),
                                   convertArgsTupleToValTypes<std::tuple<>>());
  auto func = wasm::Func::make(
      store_.get(), type.get(),
      [](void* data, const wasm::Val params[], wasm::Val[]) -> wasm::own<wasm::Trap> {
        auto func_data = reinterpret_cast<FuncData*>(data);
        ENVOY_LOG(trace, "[wasm] [vm->host] {}({})", func_data->name,
                  printValues(params, std::tuple_size<std::tuple<Args...>>::value));
        auto args_tuple = convertValTypesToArgsTuple<std::tuple<Args...>>(params);
        auto args = std::tuple_cat(std::make_tuple(current_context_), args_tuple);
        auto function = reinterpret_cast<void (*)(void*, Args...)>(func_data->raw_func);
        absl::apply(function, args);
        ENVOY_LOG(trace, "[wasm] [vm<-host] {} return: void", func_data->name);
        return nullptr;
      },
      data.get());
  data->callback = std::move(func);
  data->raw_func = reinterpret_cast<void*>(function);
  host_functions_.emplace(absl::StrCat(module_name, ".", function_name), std::move(data));
}

template <typename R, typename... Args>
void V8::registerHostFunctionImpl(absl::string_view module_name, absl::string_view function_name,
                                  R (*function)(void*, Args...)) {
  ENVOY_LOG(trace, "[wasm] registerHostFunction(\"{}.{}\")", module_name, function_name);
  auto data = std::make_unique<FuncData>(absl::StrCat(module_name, ".", function_name));
  auto type = wasm::FuncType::make(convertArgsTupleToValTypes<std::tuple<Args...>>(),
                                   convertArgsTupleToValTypes<std::tuple<R>>());
  auto func = wasm::Func::make(
      store_.get(), type.get(),
      [](void* data, const wasm::Val params[], wasm::Val results[]) -> wasm::own<wasm::Trap> {
        auto func_data = reinterpret_cast<FuncData*>(data);
        ENVOY_LOG(trace, "[wasm] [vm->host] {}({})", func_data->name,
                  printValues(params, sizeof...(Args)));
        auto args_tuple = convertValTypesToArgsTuple<std::tuple<Args...>>(params);
        auto args = std::tuple_cat(std::make_tuple(current_context_), args_tuple);
        auto function = reinterpret_cast<R (*)(void*, Args...)>(func_data->raw_func);
        R rvalue = absl::apply(function, args);
        results[0] = makeVal(rvalue);
        ENVOY_LOG(trace, "[wasm] [vm<-host] {} return: {}", func_data->name, rvalue);
        return nullptr;
      },
      data.get());
  data->callback = std::move(func);
  data->raw_func = reinterpret_cast<void*>(function);
  host_functions_.emplace(absl::StrCat(module_name, ".", function_name), std::move(data));
}

template <typename... Args>
void V8::getModuleFunctionImpl(absl::string_view function_name,
                               std::function<void(Context*, Args...)>* function) {
  ENVOY_LOG(trace, "[wasm] getModuleFunction(\"{}\")", function_name);
  auto it = module_functions_.find(function_name);
  if (it == module_functions_.end()) {
    *function = nullptr;
    return;
  }
  const wasm::Func* func = it->second.get();
  if (!equalValTypes(func->type()->params(), convertArgsTupleToValTypes<std::tuple<Args...>>()) ||
      !equalValTypes(func->type()->results(), convertArgsTupleToValTypes<std::tuple<>>())) {
    throw WasmVmException(fmt::format("Bad function signature for: {}", function_name));
  }
  *function = [func, function_name](Context* context, Args... args) -> void {
    wasm::Val params[] = {makeVal(args)...};
    ENVOY_LOG(trace, "[wasm] [host->vm] {}({})", function_name,
              printValues(params, sizeof...(Args)));
    SaveRestoreContext _saved_context(context);
    auto trap = func->call(params, nullptr);
    if (trap) {
      throw WasmVmException(
          fmt::format("Function: {} failed: {}", function_name,
                      absl::string_view(trap->message().get(), trap->message().size())));
    }
    ENVOY_LOG(trace, "[wasm] [host<-vm] {} return: void", function_name);
  };
}

template <typename R, typename... Args>
void V8::getModuleFunctionImpl(absl::string_view function_name,
                               std::function<R(Context*, Args...)>* function) {
  ENVOY_LOG(trace, "[wasm] getModuleFunction(\"{}\")", function_name);
  auto it = module_functions_.find(function_name);
  if (it == module_functions_.end()) {
    *function = nullptr;
    return;
  }
  const wasm::Func* func = it->second.get();
  if (!equalValTypes(func->type()->params(), convertArgsTupleToValTypes<std::tuple<Args...>>()) ||
      !equalValTypes(func->type()->results(), convertArgsTupleToValTypes<std::tuple<R>>())) {
    throw WasmVmException(fmt::format("Bad function signature for: {}", function_name));
  }
  *function = [func, function_name](Context* context, Args... args) -> R {
    wasm::Val params[] = {makeVal(args)...};
    ENVOY_LOG(trace, "[wasm] [host->vm] {}({})", function_name,
              printValues(params, sizeof...(Args)));
    SaveRestoreContext _saved_context(context);
    wasm::Val results[1];
    auto trap = func->call(params, results);
    if (trap) {
      throw WasmVmException(
          fmt::format("Function: {} failed: {}", function_name,
                      absl::string_view(trap->message().get(), trap->message().size())));
    }
    R rvalue = results[0].get<typename ConvertWordTypeToUint32<R>::type>();
    ENVOY_LOG(trace, "[wasm] [host<-vm] {} return: {}", function_name, rvalue);
    return rvalue;
  };
}

WasmVmPtr createVm() { return std::make_unique<V8>(); }

} // namespace V8
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
