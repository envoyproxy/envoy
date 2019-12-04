#pragma once

#include <memory>

#include "extensions/common/wasm/wasm_vm.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace Exports {

// ABI functions exported from envoy to wasm.

Word get_configuration(void* raw_context, Word address, Word size);
Word log(void* raw_context, Word level, Word address, Word size);
Word define_metric(void* raw_context, Word metric_type, Word name_ptr, Word name_size,
                   Word result_ptr);
Word increment_metric(void* raw_context, Word metric_id, int64_t offset);
Word record_metric(void* raw_context, Word metric_id, uint64_t value);
Word get_metric(void* raw_context, Word metric_id, Word result_uint64_ptr);
Word get_current_time_nanoseconds(void* raw_context, Word result_uint64_ptr);
Word set_effective_context(void* raw_context, Word context_id);
Word done(void* raw_context);

// Runtime environment functions exported from envoy to wasm.

Word wasi_unstable_fd_write(void* raw_context, Word fd, Word iovs, Word iovs_len,
                            Word nwritten_ptr);
Word wasi_unstable_fd_seek(void*, Word, int64_t, Word, Word);
Word wasi_unstable_fd_close(void*, Word);
Word wasi_unstable_environ_get(void*, Word, Word);
Word wasi_unstable_environ_sizes_get(void* raw_context, Word count_ptr, Word buf_size_ptr);
void wasi_unstable_proc_exit(void*, Word);
void wasi_unstable_proc_exit(void*, Word);
Word pthread_equal(void*, Word left, Word right);

} // namespace Exports
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
