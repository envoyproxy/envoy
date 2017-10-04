#pragma once

// NOLINT(namespace-envoy)

#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

#define htole32(x) OSSwapHostToLittleInt32((x))
#define htole64(x) OSSwapHostToLittleInt64((x))
#define le32toh(x) OSSwapLittleToHostInt32((x))
#define le64toh(x) OSSwapLittleToHostInt64((x))

#else

#include <endian.h>

#endif
