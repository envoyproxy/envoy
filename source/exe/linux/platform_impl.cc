#if !defined(__linux__)
#error "Linux platform file is part of non-Linux build."
#endif

#include <sys/prctl.h>

#include "common/common/thread_impl.h"
#include "common/filesystem/filesystem_impl.h"

#include "exe/platform_impl.h"

namespace Envoy {

PlatformImpl::PlatformImpl()
    : thread_factory_(std::make_unique<Thread::ThreadFactoryImplPosix>()),
      file_system_(std::make_unique<Filesystem::InstanceImplPosix>()) {}

PlatformImpl::~PlatformImpl() = default;

bool PlatformImpl::enableCoreDump() { return prctl(PR_SET_DUMPABLE, 1) != -1; }

} // namespace Envoy
