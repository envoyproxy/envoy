// NOLINT(namespace-envoy)
#if defined(__linux__)
#include "common/filesystem/watcher_impl_linux.h"

#elif defined(__FreeBSD__) || defined(__APPLE__)
#include "common/filesystem/watcher_impl_bsd.h"
#endif
