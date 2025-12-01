# BUILD file for nghttp2 used in WORKSPACE mode
# This mirrors the BCR module's BUILD.bazel overlay

load("@bazel_skylib//rules:expand_template.bzl", "expand_template")
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

exports_files(["COPYING"])

cc_library(
    name = "nghttp2",
    hdrs = [
        "lib/includes/nghttp2/nghttp2.h",
        ":nghttp2ver_h",
    ],
    defines = [
        "NGHTTP2_STATICLIB",
    ],
    strip_include_prefix = "lib/includes",
    deps = [
        ":nghttp2_impl",
    ],
)

cc_library(
    name = "nghttp2_impl",
    srcs = glob(["lib/*.c"]),
    hdrs = glob(["lib/*.h"]),
    copts = select({
        "@rules_cc//cc/compiler:msvc-cl": [],
        "//conditions:default": [
            "-Wno-string-plus-int",
        ],
    }),
    local_defines = [
        "BUILDING_NGHTTP2",
        "HAVE_CONFIG_H",
    ],
    deps = [
        ":nghttp2_config",
        ":nghttp2_headers",
    ],
)

cc_library(
    name = "nghttp2_headers",
    hdrs = [
        "lib/includes/nghttp2/nghttp2.h",
        ":nghttp2ver_h",
    ],
    defines = [
        "NGHTTP2_STATICLIB",
    ],
    strip_include_prefix = "lib/includes",
)

cc_library(
    name = "nghttp2_config",
    hdrs = [
        ":config_h",
    ],
    strip_include_prefix = "lib",
)

expand_template(
    name = "nghttp2ver_h",
    out = "lib/includes/nghttp2/nghttp2ver.h",
    substitutions = {
        "@PACKAGE_VERSION@": "1.66.0",
        "@PACKAGE_VERSION_NUM@": "0x014200",
    },
    template = "lib/includes/nghttp2/nghttp2ver.h.in",
)

genrule(
    name = "config_h",
    outs = ["lib/config.h"],
    cmd = """
cat > $@ << 'EOF'
#ifndef EXTERNAL_NGHTTP2_CONFIG_H_
#define EXTERNAL_NGHTTP2_CONFIG_H_

#if !defined(_WIN32) && !defined(__APPLE__)
#include <stdint.h>
#endif

#define HAVE_STD_MAP_EMPLACE 1
#define HAVE__EXIT 1
#define HAVE_INTTYPES_H 1
#define HAVE_LIMITS_H 1
#define HAVE_FCNTL_H 1
#define HAVE_TIME_H 1
#define NGHTTP2_NORETURN __attribute__((noreturn))

#if defined(_WIN32)
#include <stddef.h>
#define ssize_t ptrdiff_t
#define HAVE_DECL_INITGROUPS 0

#elif defined(__APPLE__)
#define HAVE_ARPA_INET_H 1
#define HAVE_SOCKADDR_IN6_SIN6_LEN 1
#define HAVE_SOCKADDR_IN_SIN_LEN 1
#define SIZEOF_TIME_T 8
#define STDC_HEADERS 1

#else
#define HAVE_ACCEPT4 1
#define HAVE_ARPA_INET_H 1
#endif

// common linux, apple
#if !defined(_WIN32)
#define HAVE_ATOMIC_STD_SHARED_PTR 1
#define HAVE_CHOWN 1
#define HAVE_CXX14 1
#define HAVE_DECL_INITGROUPS 1
#define HAVE_DECL_STRERROR_R 1
#define HAVE_DLFCN_H 1
#define HAVE_DUP2 1
#define HAVE_FORK 1
#define HAVE_GETCWD 1
#define HAVE_GETPWNAM 1
#define HAVE_LOCALTIME_R 1
#define HAVE_MEMCHR 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMORY_H 1
#define HAVE_MEMSET 1
#define HAVE_MKOSTEMP 1
#define HAVE_NETDB_H 1
#define HAVE_NETINET_IN_H 1
#define HAVE_PTRDIFF_T 1
#define HAVE_PWD_H 1
#define HAVE_SOCKET 1
#define HAVE_SQRT 1
#define HAVE_STD_FUTURE 1
#define HAVE_STDDEF_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRERROR_R 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRNDUP 1
#define HAVE_STRSTR 1
#define HAVE_STRTOL 1
#define HAVE_STRTOUL 1
#define HAVE_STRUCT_TM_TM_GMTOFF 1
#define HAVE_SYS_SOCKET_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYSLOG_H 1
#define HAVE_THREAD_LOCAL 1
#define HAVE_TIMEGM 1
#define HAVE_UNISTD_H 1
#define HAVE_VFORK 1
#define HAVE_WORKING_FORK 1
#define HAVE_WORKING_VFORK 1
#endif

#if !defined(_WIN32)
#ifndef _ALL_SOURCE
#define _ALL_SOURCE 1
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#ifndef _POSIX_PTHREAD_SEMANTICS
#define _POSIX_PTHREAD_SEMANTICS 1
#endif
#ifndef _TANDEM_SOURCE
#define _TANDEM_SOURCE 1
#endif
#ifndef __EXTENSIONS__
#define __EXTENSIONS__ 1
#endif
#endif

#if defined(__APPLE__)
#if defined AC_APPLE_UNIVERSAL_BUILD
#if defined __BIG_ENDIAN__
#define WORDS_BIGENDIAN 1
#endif
#endif
#ifndef _DARWIN_USE_64_BIT_INODE
#define _DARWIN_USE_64_BIT_INODE 1
#endif
#endif

#if UINTPTR_MAX == UINT64_MAX
#define SIZEOF_INT_P 8
#elif UINTPTR_MAX == UINT32_MAX
#define SIZEOF_INT_P 4
#else
#error "Unknown int pointer size"
#endif

#endif  // EXTERNAL_NGHTTP2_CONFIG_H_
EOF
""",
)
