// NOLINT(namespace-envoy)
#pragma once

#if !defined(__linux__)
#define UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT 0
#else
#define UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT 1
#endif
