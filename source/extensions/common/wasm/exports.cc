#include "extensions/common/wasm/wasm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Exports {

// Any currently executing Wasm call context.
#define WASM_CONTEXT(_c)                                                                           \
  (ContextOrEffectiveContext(static_cast<Context*>((void)_c, current_context_)))
// The id of the context which should be used for calls out of the VM in place of current_context_
// above.

namespace {

inline Word wasmResultToWord(WasmResult r) { return Word(static_cast<uint64_t>(r)); }

Context* ContextOrEffectiveContext(Context* context) {
  if (effective_context_id_ == 0) {
    return context;
  }
  auto effective_context = context->wasm()->getContext(effective_context_id_);
  if (effective_context) {
    return effective_context;
  }
  // The effective_context_id_ no longer exists, revert to the true context.
  return context;
}

} // namespace

Word get_configuration(void* raw_context, Word value_ptr_ptr, Word value_size_ptr) {
  auto context = WASM_CONTEXT(raw_context);
  auto value = context->getConfiguration();
  if (!context->wasm()->copyToPointerSize(value, value_ptr_ptr.u64_, value_size_ptr.u64_)) {
    return wasmResultToWord(WasmResult::InvalidMemoryAccess);
  }
  return wasmResultToWord(WasmResult::Ok);
}

Word set_effective_context(void* raw_context, Word context_id) {
  auto context = WASM_CONTEXT(raw_context);
  uint32_t cid = static_cast<uint32_t>(context_id.u64_);
  auto c = context->wasm()->getContext(cid);
  if (!c) {
    return wasmResultToWord(WasmResult::BadArgument);
  }
  effective_context_id_ = cid;
  return wasmResultToWord(WasmResult::Ok);
}

Word done(void* raw_context) {
  auto context = WASM_CONTEXT(raw_context);
  return wasmResultToWord(context->wasm()->done(context));
}

Word define_metric(void* raw_context, Word metric_type, Word name_ptr, Word name_size,
                   Word metric_id_ptr) {
  if (metric_type.u64_ > static_cast<uint64_t>(Context::MetricType::Max)) {
    return wasmResultToWord(WasmResult::BadArgument);
  }
  auto context = WASM_CONTEXT(raw_context);
  auto name = context->wasmVm()->getMemory(name_ptr.u64_, name_size.u64_);
  if (!name) {
    return wasmResultToWord(WasmResult::InvalidMemoryAccess);
  }
  uint32_t metric_id = 0;
  auto result = context->defineMetric(static_cast<Context::MetricType>(metric_type.u64_),
                                      name.value(), &metric_id);
  if (result != WasmResult::Ok) {
    return wasmResultToWord(result);
  }
  if (!context->wasm()->setDatatype(metric_id_ptr.u64_, metric_id)) {
    return wasmResultToWord(WasmResult::InvalidMemoryAccess);
  }
  return wasmResultToWord(WasmResult::Ok);
}

Word increment_metric(void* raw_context, Word metric_id, int64_t offset) {
  auto context = WASM_CONTEXT(raw_context);
  return wasmResultToWord(context->incrementMetric(metric_id.u64_, offset));
}

Word record_metric(void* raw_context, Word metric_id, uint64_t value) {
  auto context = WASM_CONTEXT(raw_context);
  return wasmResultToWord(context->recordMetric(metric_id.u64_, value));
}

Word get_metric(void* raw_context, Word metric_id, Word result_uint64_ptr) {
  auto context = WASM_CONTEXT(raw_context);
  uint64_t value = 0;
  auto result = context->getMetric(metric_id.u64_, &value);
  if (result != WasmResult::Ok) {
    return wasmResultToWord(result);
  }
  if (!context->wasm()->setDatatype(result_uint64_ptr.u64_, value)) {
    return wasmResultToWord(WasmResult::InvalidMemoryAccess);
  }
  return wasmResultToWord(WasmResult::Ok);
}

// Implementation of writev-like() syscall that redirects stdout/stderr to Envoy logs.
Word writevImpl(void* raw_context, Word fd, Word iovs, Word iovs_len, Word* nwritten_ptr) {
  auto context = WASM_CONTEXT(raw_context);

  // Read syscall args.
  spdlog::level::level_enum log_level;
  switch (fd.u64_) {
  case 1 /* stdout */:
    log_level = spdlog::level::info;
    break;
  case 2 /* stderr */:
    log_level = spdlog::level::err;
    break;
  default:
    return 8; // __WASI_EBADF
  }

  std::string s;
  for (size_t i = 0; i < iovs_len.u64_; i++) {
    auto memslice =
        context->wasmVm()->getMemory(iovs.u64_ + i * 2 * sizeof(uint32_t), 2 * sizeof(uint32_t));
    if (!memslice) {
      return 21; // __WASI_EFAULT
    }
    const uint32_t* iovec = reinterpret_cast<const uint32_t*>(memslice.value().data());
    if (iovec[1] /* buf_len */) {
      memslice = context->wasmVm()->getMemory(iovec[0] /* buf */, iovec[1] /* buf_len */);
      if (!memslice) {
        return 21; // __WASI_EFAULT
      }
      s.append(memslice.value().data(), memslice.value().size());
    }
  }

  size_t written = s.size();
  if (written) {
    // Remove trailing newline from the logs, if any.
    if (s[written - 1] == '\n') {
      s.erase(written - 1);
    }
    context->scriptLog(log_level, s);
  }
  *nwritten_ptr = Word(written);
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_fd_write(_wasi_fd_t fd, const _wasi_ciovec_t *iov, size_t iovs_len, size_t*
// nwritten);
Word wasi_unstable_fd_write(void* raw_context, Word fd, Word iovs, Word iovs_len,
                            Word nwritten_ptr) {
  auto context = WASM_CONTEXT(raw_context);

  Word nwritten(0);
  auto result = writevImpl(raw_context, fd, iovs, iovs_len, &nwritten);
  if (result.u64_ != 0) { // __WASI_ESUCCESS
    return result;
  }
  if (!context->wasmVm()->setWord(nwritten_ptr.u64_, Word(nwritten))) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_fd_seek(__wasi_fd_t fd, __wasi_filedelta_t offset, __wasi_whence_t
// whence,__wasi_filesize_t *newoffset);
Word wasi_unstable_fd_seek(void*, Word, int64_t, Word, Word) {
  throw WasmException("wasi_unstable fd_seek");
}

// __wasi_errno_t __wasi_fd_close(__wasi_fd_t fd);
Word wasi_unstable_fd_close(void*, Word) { throw WasmException("wasi_unstable fd_close"); }

// __wasi_errno_t __wasi_environ_get(char **environ, char *environ_buf);
Word wasi_unstable_environ_get(void*, Word, Word) {
  return 0; // __WASI_ESUCCESS
}

// __wasi_errno_t __wasi_environ_sizes_get(size_t *environ_count, size_t *environ_buf_size);
Word wasi_unstable_environ_sizes_get(void* raw_context, Word count_ptr, Word buf_size_ptr) {
  auto context = WASM_CONTEXT(raw_context);
  if (!context->wasmVm()->setWord(count_ptr.u64_, Word(0))) {
    return 21; // __WASI_EFAULT
  }
  if (!context->wasmVm()->setWord(buf_size_ptr.u64_, Word(0))) {
    return 21; // __WASI_EFAULT
  }
  return 0; // __WASI_ESUCCESS
}

// void __wasi_proc_exit(__wasi_exitcode_t rval);
void wasi_unstable_proc_exit(void*, Word) { throw WasmException("wasi_unstable proc_exit"); }

Word pthread_equal(void*, Word left, Word right) { return left.u64_ == right.u64_; }

Word get_current_time_nanoseconds(void* raw_context, Word result_uint64_ptr) {
  auto context = WASM_CONTEXT(raw_context);
  uint64_t result = context->getCurrentTimeNanoseconds();
  if (!context->wasm()->setDatatype(result_uint64_ptr.u64_, result)) {
    return wasmResultToWord(WasmResult::InvalidMemoryAccess);
  }
  return wasmResultToWord(WasmResult::Ok);
}

Word log(void* raw_context, Word level, Word address, Word size) {
  auto context = WASM_CONTEXT(raw_context);
  auto message = context->wasmVm()->getMemory(address.u64_, size.u64_);
  if (!message) {
    return wasmResultToWord(WasmResult::InvalidMemoryAccess);
  }
  context->scriptLog(static_cast<spdlog::level::level_enum>(level.u64_), message.value());
  return wasmResultToWord(WasmResult::Ok);
}

} // namespace Exports
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
