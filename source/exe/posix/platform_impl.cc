#include "source/common/common/thread_impl.h"
#include "source/common/filesystem/filesystem_impl.h"
#include "source/exe/platform_impl.h"

namespace Envoy {

PlatformImpl::PlatformImpl()
    : thread_factory_(Thread::PosixThreadFactory::create()),
      file_system_(std::make_unique<Filesystem::InstanceImplPosix>()) {}

PlatformImpl::~PlatformImpl() = default;

bool PlatformImpl::enableCoreDump() { return false; }

} // namespace Envoy
