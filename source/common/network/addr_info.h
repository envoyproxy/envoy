#pragma once

#include "common/common/c_smart_ptr.h"

namespace Network {

typedef CSmartPtr<addrinfo, freeaddrinfo> AddrInfoPtr;

} // Network
