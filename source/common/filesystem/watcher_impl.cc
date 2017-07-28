// NOLINT(namespace-envoy)
#if defined(__linux__)
#include "common/filesystem/watcher_impl_linux.cc"

#elif defined(__APPLE__)
#include "common/filesystem/watcher_impl_bsd.cc"
#endif
