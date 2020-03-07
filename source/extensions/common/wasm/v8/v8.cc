#include "extensions/common/wasm/v8/v8.h"

#include <memory>
#include <utility>
#include <vector>

#include "common/common/assert.h"

#include "extensions/common/wasm/wasm_vm_base.h"
#include "extensions/common/wasm/well_known_names.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "v8-version.h"
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
  FuncData(std::string name) : name_(std::move(name)) {}

  std::string name_;
  wasm::own<wasm::Func> callback_;
  void* raw_func_;
};

using FuncDataPtr = std::unique_ptr<FuncData>;

class V8 : public WasmVmBase {
public:
  V8(const Stats::ScopeSharedPtr& scope) : WasmVmBase(scope, WasmRuntimeNames::get().V8) {}

  // Extensions::Common::Wasm::WasmVm
  absl::string_view runtime() override { return WasmRuntimeNames::get().V8; }

  bool load(const std::string& code, bool allow_precompiled) override;
  absl::string_view getCustomSection(absl::string_view name) override;
  absl::string_view getPrecompiledSectionName() override;
  void link(absl::string_view debug_name) override;

  Cloneable cloneable() override { return Cloneable::CompiledBytecode; }
  WasmVmPtr clone() override;

  uint64_t getMemorySize() override;
  absl::optional<absl::string_view> getMemory(uint64_t pointer, uint64_t size) override;
  bool setMemory(uint64_t pointer, uint64_t size, const void* data) override;
  bool getWord(uint64_t pointer, Word* word) override;
  bool setWord(uint64_t pointer, Word word) override;

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
  wasm::vec<byte_t> getStrippedSource();

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
  wasm::own<wasm::Shared<wasm::Module>> shared_module_;
  wasm::own<wasm::Instance> instance_;
  wasm::own<wasm::Memory> memory_;
  wasm::own<wasm::Table> table_;

  absl::flat_hash_map<std::string, FuncDataPtr> host_functions_;
  absl::flat_hash_map<std::string, wasm::own<wasm::Func>> module_functions_;
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

bool V8::load(const std::string& code, bool allow_precompiled) {
  ENVOY_LOG(trace, "load()");
  store_ = wasm::Store::make(engine());

  // Wasm file header is 8 bytes (magic number + version).
  static const uint8_t magic_number[4] = {0x00, 0x61, 0x73, 0x6d};
  if (code.size() < 8 || ::memcmp(code.data(), magic_number, 4) != 0) {
    return false;
  }

  source_ = wasm::vec<byte_t>::make_uninitialized(code.size());
  ::memcpy(source_.get(), code.data(), code.size());

  if (allow_precompiled) {
    const auto section_name = getPrecompiledSectionName();
    if (!section_name.empty()) {
      const auto precompiled = getCustomSection(section_name);
      if (!precompiled.empty()) {
        auto vec = wasm::vec<byte_t>::make_uninitialized(precompiled.size());
        ::memcpy(vec.get(), precompiled.data(), precompiled.size());

        // TODO(PiotrSikora): fuzz loading of precompiled Wasm modules.
        // See: https://github.com/envoyproxy/envoy/issues/9731
        module_ = wasm::Module::deserialize(store_.get(), vec);
        if (!module_) {
          // Precompiled module that cannot be loaded is considered a hard error,
          // so don't fallback to compiling the bytecode.
          return false;
        }
      }
    }
  }

  if (!module_) {
    // TODO(PiotrSikora): fuzz loading of Wasm modules.
    // See: https://github.com/envoyproxy/envoy/issues/9731
    const auto stripped_source = getStrippedSource();
    module_ = wasm::Module::make(store_.get(), stripped_source ? stripped_source : source_);
  }

  if (module_) {
    shared_module_ = module_->share();
  }

  return module_ != nullptr;
}

WasmVmPtr V8::clone() {
  ENVOY_LOG(trace, "clone()");
  ASSERT(shared_module_ != nullptr);

  auto clone = std::make_unique<V8>(scope_);
  clone->store_ = wasm::Store::make(engine());

  clone->module_ = wasm::Module::obtain(clone->store_.get(), shared_module_.get());

  return clone;
}

// Get Wasm module without Custom Sections to save some memory in workers.
wasm::vec<byte_t> V8::getStrippedSource() {
  ENVOY_LOG(trace, "getStrippedSource()");
  ASSERT(source_.get() != nullptr);

  std::vector<byte_t> stripped;

  const byte_t* pos = source_.get() + 8 /* Wasm header */;
  const byte_t* end = source_.get() + source_.size();
  while (pos < end) {
    const auto section_start = pos;
    if (pos + 1 > end) {
      return wasm::vec<byte_t>::invalid();
    }
    const auto section_type = *pos++;
    const auto section_len = parseVarint(pos, end);
    if (section_len == static_cast<uint32_t>(-1) || pos + section_len > end) {
      return wasm::vec<byte_t>::invalid();
    }
    pos += section_len;
    if (section_type == 0 /* custom section */) {
      if (stripped.empty()) {
        const byte_t* start = source_.get();
        stripped.insert(stripped.end(), start, section_start);
      }
    } else if (!stripped.empty()) {
      stripped.insert(stripped.end(), section_start, pos /* section end */);
    }
  }

  // No custom sections found, use the original source.
  if (stripped.empty()) {
    return wasm::vec<byte_t>::invalid();
  }

  // Return stripped source, without custom sections.
  return wasm::vec<byte_t>::make(stripped.size(), stripped.data());
}

absl::string_view V8::getCustomSection(absl::string_view name) {
  ENVOY_LOG(trace, "getCustomSection(\"{}\")", name);
  ASSERT(source_.get() != nullptr);

  const byte_t* pos = source_.get() + 8 /* Wasm header */;
  const byte_t* end = source_.get() + source_.size();
  while (pos < end) {
    if (pos + 1 > end) {
      throw WasmVmException("Failed to parse corrupted WASM module");
    }
    const auto section_type = *pos++;
    const auto section_len = parseVarint(pos, end);
    if (section_len == static_cast<uint32_t>(-1) || pos + section_len > end) {
      throw WasmVmException("Failed to parse corrupted WASM module");
    }
    if (section_type == 0 /* custom section */) {
      const auto section_data_start = pos;
      const auto section_name_len = parseVarint(pos, end);
      if (section_name_len == static_cast<uint32_t>(-1) || pos + section_name_len > end) {
        throw WasmVmException("Failed to parse corrupted WASM module");
      }
      if (section_name_len == name.size() && ::memcmp(pos, name.data(), section_name_len) == 0) {
        pos += section_name_len;
        ENVOY_LOG(trace, "getCustomSection(\"{}\") found, size: {}", name,
                  section_data_start + section_len - pos);
        return {pos, static_cast<size_t>(section_data_start + section_len - pos)};
      }
      pos = section_data_start + section_len;
    } else {
      pos += section_len;
    }
  }
  return "";
}

#if defined(__linux__) && defined(__x86_64__)
#define WEE8_WASM_PRECOMPILE_PLATFORM "linux_x86_64"
#endif

absl::string_view V8::getPrecompiledSectionName() {
#ifndef WEE8_WASM_PRECOMPILE_PLATFORM
  return "";
#else
  static const auto name =
      absl::StrCat("precompiled_v8_v", V8_MAJOR_VERSION, ".", V8_MINOR_VERSION, ".",
                   V8_BUILD_NUMBER, ".", V8_PATCH_LEVEL, "_", WEE8_WASM_PRECOMPILE_PLATFORM);
  return name;
#endif
}

void V8::link(absl::string_view debug_name) {
  ENVOY_LOG(trace, "link(\"{}\")", debug_name);
  ASSERT(module_ != nullptr);

  const auto import_types = module_.get()->imports();
  std::vector<const wasm::Extern*> imports;

  for (size_t i = 0; i < import_types.size(); i++) {
    absl::string_view module(import_types[i]->module().get(), import_types[i]->module().size());
    absl::string_view name(import_types[i]->name().get(), import_types[i]->name().size());
    auto import_type = import_types[i]->type();

    switch (import_type->kind()) {

    case wasm::EXTERN_FUNC: {
      ENVOY_LOG(trace, "link(), export host func: {}.{} ({} -> {})", module, name,
                printValTypes(import_type->func()->params()),
                printValTypes(import_type->func()->results()));

      auto it = host_functions_.find(absl::StrCat(module, ".", name));
      if (it == host_functions_.end()) {
        throw WasmVmException(
            fmt::format("Failed to load WASM module due to a missing import: {}.{}", module, name));
      }
      auto func = it->second.get()->callback_.get();
      if (!equalValTypes(import_type->func()->params(), func->type()->params()) ||
          !equalValTypes(import_type->func()->results(), func->type()->results())) {
        throw WasmVmException(fmt::format(
            "Failed to load WASM module due to an import type mismatch: {}.{}, "
            "want: {} -> {}, but host exports: {} -> {}",
            module, name, printValTypes(import_type->func()->params()),
            printValTypes(import_type->func()->results()), printValTypes(func->type()->params()),
            printValTypes(func->type()->results())));
      }
      imports.push_back(func);
    } break;

    case wasm::EXTERN_GLOBAL: {
      // TODO(PiotrSikora): add support when/if needed.
      ENVOY_LOG(trace, "link(), export host global: {}.{} ({})", module, name,
                printValKind(import_type->global()->content()->kind()));

      throw WasmVmException(
          fmt::format("Failed to load WASM module due to a missing import: {}.{}", module, name));
    } break;

    case wasm::EXTERN_MEMORY: {
      ENVOY_LOG(trace, "link(), export host memory: {}.{} (min: {} max: {})", module, name,
                import_type->memory()->limits().min, import_type->memory()->limits().max);

      ASSERT(memory_ == nullptr);
      auto type = wasm::MemoryType::make(import_type->memory()->limits());
      memory_ = wasm::Memory::make(store_.get(), type.get());
      imports.push_back(memory_.get());
    } break;

    case wasm::EXTERN_TABLE: {
      ENVOY_LOG(trace, "link(), export host table: {}.{} (min: {} max: {})", module, name,
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
      ENVOY_LOG(trace, "link(), import module func: {} ({} -> {})", name,
                printValTypes(export_type->func()->params()),
                printValTypes(export_type->func()->results()));

      ASSERT(export_item->func() != nullptr);
      module_functions_.insert_or_assign(name, export_item->func()->copy());
    } break;

    case wasm::EXTERN_GLOBAL: {
      // TODO(PiotrSikora): add support when/if needed.
      ENVOY_LOG(trace, "link(), import module global: {} ({}) --- IGNORED", name,
                printValKind(export_type->global()->content()->kind()));
    } break;

    case wasm::EXTERN_MEMORY: {
      ENVOY_LOG(trace, "link(), import module memory: {} (min: {} max: {})", name,
                export_type->memory()->limits().min, export_type->memory()->limits().max);

      ASSERT(export_item->memory() != nullptr);
      ASSERT(memory_ == nullptr);
      memory_ = exports[i]->memory()->copy();
    } break;

    case wasm::EXTERN_TABLE: {
      // TODO(PiotrSikora): add support when/if needed.
      ENVOY_LOG(trace, "link(), import module table: {} (min: {} max: {}) --- IGNORED", name,
                export_type->table()->limits().min, export_type->table()->limits().max);
    } break;
    }
  }
}

uint64_t V8::getMemorySize() {
  ENVOY_LOG(trace, "getMemorySize()");
  return memory_->data_size();
}

absl::optional<absl::string_view> V8::getMemory(uint64_t pointer, uint64_t size) {
  ENVOY_LOG(trace, "getMemory({}, {})", pointer, size);
  ASSERT(memory_ != nullptr);
  if (pointer + size > memory_->data_size()) {
    return absl::nullopt;
  }
  return absl::string_view(memory_->data() + pointer, size);
}

bool V8::setMemory(uint64_t pointer, uint64_t size, const void* data) {
  ENVOY_LOG(trace, "setMemory({}, {})", pointer, size);
  ASSERT(memory_ != nullptr);
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  ::memcpy(memory_->data() + pointer, data, size);
  return true;
}

bool V8::getWord(uint64_t pointer, Word* word) {
  ENVOY_LOG(trace, "getWord({})", pointer);
  constexpr auto size = sizeof(uint32_t);
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  uint32_t word32;
  ::memcpy(&word32, memory_->data() + pointer, size);
  word->u64_ = word32;
  return true;
}

bool V8::setWord(uint64_t pointer, Word word) {
  ENVOY_LOG(trace, "setWord({}, {})", pointer, word.u64_);
  constexpr auto size = sizeof(uint32_t);
  if (pointer + size > memory_->data_size()) {
    return false;
  }
  uint32_t word32 = word.u32();
  ::memcpy(memory_->data() + pointer, &word32, size);
  return true;
}

template <typename... Args>
void V8::registerHostFunctionImpl(absl::string_view module_name, absl::string_view function_name,
                                  void (*function)(void*, Args...)) {
  ENVOY_LOG(trace, "registerHostFunction(\"{}.{}\")", module_name, function_name);
  auto data = std::make_unique<FuncData>(absl::StrCat(module_name, ".", function_name));
  auto type = wasm::FuncType::make(convertArgsTupleToValTypes<std::tuple<Args...>>(),
                                   convertArgsTupleToValTypes<std::tuple<>>());
  auto func = wasm::Func::make(
      store_.get(), type.get(),
      [](void* data, const wasm::Val params[], wasm::Val[]) -> wasm::own<wasm::Trap> {
        auto func_data = reinterpret_cast<FuncData*>(data);
        ENVOY_LOG(trace, "[vm->host] {}({})", func_data->name_,
                  printValues(params, std::tuple_size<std::tuple<Args...>>::value));
        auto args_tuple = convertValTypesToArgsTuple<std::tuple<Args...>>(params);
        auto args = std::tuple_cat(std::make_tuple(current_context_), args_tuple);
        auto function = reinterpret_cast<void (*)(void*, Args...)>(func_data->raw_func_);
        absl::apply(function, args);
        ENVOY_LOG(trace, "[vm<-host] {} return: void", func_data->name_);
        return nullptr;
      },
      data.get());
  data->callback_ = std::move(func);
  data->raw_func_ = reinterpret_cast<void*>(function);
  host_functions_.insert_or_assign(absl::StrCat(module_name, ".", function_name), std::move(data));
}

template <typename R, typename... Args>
void V8::registerHostFunctionImpl(absl::string_view module_name, absl::string_view function_name,
                                  R (*function)(void*, Args...)) {
  ENVOY_LOG(trace, "registerHostFunction(\"{}.{}\")", module_name, function_name);
  auto data = std::make_unique<FuncData>(absl::StrCat(module_name, ".", function_name));
  auto type = wasm::FuncType::make(convertArgsTupleToValTypes<std::tuple<Args...>>(),
                                   convertArgsTupleToValTypes<std::tuple<R>>());
  auto func = wasm::Func::make(
      store_.get(), type.get(),
      [](void* data, const wasm::Val params[], wasm::Val results[]) -> wasm::own<wasm::Trap> {
        auto func_data = reinterpret_cast<FuncData*>(data);
        ENVOY_LOG(trace, "[vm->host] {}({})", func_data->name_,
                  printValues(params, sizeof...(Args)));
        auto args_tuple = convertValTypesToArgsTuple<std::tuple<Args...>>(params);
        auto args = std::tuple_cat(std::make_tuple(current_context_), args_tuple);
        auto function = reinterpret_cast<R (*)(void*, Args...)>(func_data->raw_func_);
        R rvalue = absl::apply(function, args);
        results[0] = makeVal(rvalue);
        ENVOY_LOG(trace, "[vm<-host] {} return: {}", func_data->name_, rvalue);
        return nullptr;
      },
      data.get());
  data->callback_ = std::move(func);
  data->raw_func_ = reinterpret_cast<void*>(function);
  host_functions_.insert_or_assign(absl::StrCat(module_name, ".", function_name), std::move(data));
}

template <typename... Args>
void V8::getModuleFunctionImpl(absl::string_view function_name,
                               std::function<void(Context*, Args...)>* function) {
  ENVOY_LOG(trace, "getModuleFunction(\"{}\")", function_name);
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
    ENVOY_LOG(trace, "[host->vm] {}({})", function_name, printValues(params, sizeof...(Args)));
    SaveRestoreContext saved_context(context);
    auto trap = func->call(params, nullptr);
    if (trap) {
      throw WasmException(
          fmt::format("Function: {} failed: {}", function_name,
                      absl::string_view(trap->message().get(), trap->message().size())));
    }
    ENVOY_LOG(trace, "[host<-vm] {} return: void", function_name);
  };
}

template <typename R, typename... Args>
void V8::getModuleFunctionImpl(absl::string_view function_name,
                               std::function<R(Context*, Args...)>* function) {
  ENVOY_LOG(trace, "getModuleFunction(\"{}\")", function_name);
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
    wasm::Val results[1];
    ENVOY_LOG(trace, "[host->vm] {}({})", function_name, printValues(params, sizeof...(Args)));
    SaveRestoreContext saved_context(context);
    auto trap = func->call(params, results);
    if (trap) {
      throw WasmException(
          fmt::format("Function: {} failed: {}", function_name,
                      absl::string_view(trap->message().get(), trap->message().size())));
    }
    R rvalue = results[0].get<typename ConvertWordTypeToUint32<R>::type>();
    ENVOY_LOG(trace, "[host<-vm] {} return: {}", function_name, rvalue);
    return rvalue;
  };
}

WasmVmPtr createVm(const Stats::ScopeSharedPtr& scope) { return std::make_unique<V8>(scope); }

} // namespace V8
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
