def boringssl_repositories():
    native.git_repository(
        name = "boringssl",
        commit = "bfd36df3da38dbf8828e712f42fbab2a0034bc40",  # 2017-02-02
        remote = "https://boringssl.googlesource.com/boringssl",
    )

def protobuf_repositories():
    native.git_repository(
        name = "protobuf",
        commit = "a428e42072765993ff674fda72863c9f1aa2d268",  # v3.1.0
        remote = "https://github.com/google/protobuf.git",
    )

def googletest_repositories():
    BUILD = """
# Copyright 2016 Google Inc. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
################################################################################
#

cc_library(
    name = "googletest",
    srcs = [
        "googlemock/src/gmock-all.cc",
        "googletest/src/gtest-all.cc",
    ],
    hdrs = glob([
        "googletest/include/**/*.h",
        "googlemock/include/**/*.h",
        "googletest/src/*.cc",
        "googletest/src/*.h",
        "googlemock/src/*.cc",
    ]),
    includes = [
        "googlemock",
        "googlemock/include",
        "googletest",
        "googletest/include",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "googletest_main",
    srcs = ["googlemock/src/gmock_main.cc"],
    visibility = ["//visibility:public"],
    deps = [":googletest"],
)

cc_library(
    name = "googletest_prod",
    hdrs = [
        "googletest/include/gtest/gtest_prod.h",
    ],
    includes = [
        "googletest/include",
    ],
    visibility = ["//visibility:public"],
)
"""
    native.new_git_repository(
        name = "googletest",
        build_file_content = BUILD,
        commit = "d225acc90bc3a8c420a9bcd1f033033c1ccd7fe0",
        remote = "https://github.com/google/googletest.git",
    )

def libevent_repositories():
    BUILD = """
genrule(
    name = "config",
    srcs = glob([
        "**/*",
    ]),
    outs = [
        "config.h",
    ],
    cmd = "$(location configure) --enable-shared=no --disable-libevent-regress --disable-openssl" +
          " && cp config.h $@",
    tools = [
        "configure",
    ],
)

genrule(
    name = "event-config",
    srcs = [
        "config.h",
        "make-event-config.sed",
    ],
    outs = [
        "include/event2/event-config.h",
    ],
    cmd = "sed -f $(location make-event-config.sed) < $(location config.h) > $@",
)

event_srcs = [
    "buffer.c",
    "bufferevent.c",
    "bufferevent_filter.c",
    "bufferevent_pair.c",
    "bufferevent_ratelim.c",
    "bufferevent_sock.c",
    "epoll.c",
    "evdns.c",
    "event.c",
    "event_tagging.c",
    "evmap.c",
    "evrpc.c",
    "evthread.c",
    "evutil.c",
    "evutil_rand.c",
    "http.c",
    "listener.c",
    "log.c",
    "poll.c",
    "select.c",
    "signal.c",
    "strlcpy.c",
    ":event-config",
] + glob(["*.h"])

event_pthread_srcs = [
    "evthread_pthread.c",
    ":event-config",
]

cc_library(
    name = "event",
    srcs = event_srcs,
    hdrs = glob(["include/**/*.h"]) + [
        "arc4random.c",  # arc4random.c is included by evutil_rand.c
        "bufferevent-internal.h",
        "defer-internal.h",
        "evbuffer-internal.h",
        "event-internal.h",
        "event.h",
        "evthread-internal.h",
        "evutil.h",
        "http-internal.h",
        "iocp-internal.h",
        "ipv6-internal.h",
        "log-internal.h",
        "minheap-internal.h",
        "mm-internal.h",
        "strlcpy-internal.h",
        "util-internal.h",
        "compat/sys/queue.h",
    ],
    copts = [
        "-w",
        "-DHAVE_CONFIG_H",
    ],
    includes = [
        "compat",
        "include",
    ],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "event_pthreads",
    srcs = event_pthread_srcs + ["include/event2/thread.h"],
    hdrs = [
        "compat/sys/queue.h",
        "evthread-internal.h",
    ],
    copts = [
        "-w",
        "-DHAVE_CONFIG_H",
    ],
    includes = [
        "compat",
        "include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":event",
    ],
)
"""

    native.new_http_archive(
        name = "libevent",
        url = "https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz",
        strip_prefix = "libevent-2.0.22-stable",
        build_file_content = BUILD,
    )

def spdlog_repositories():
    BUILD = """
package(default_visibility=["//visibility:public"])

cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.h",
        "include/**/*.cc",
    ]),
    includes = [
        "include",
    ],
)
"""

    native.new_git_repository(
        name = "spdlog",
        remote = "https://github.com/gabime/spdlog.git",
        commit = "1f1f6a5f3b424203a429e9cb78e6548037adefa8",
        build_file_content = BUILD,
    )

def tclap_repositories():
    BUILD = """
cc_library(
    name = "tclap",
    hdrs = [
        "include/tclap/Arg.h",
        "include/tclap/ArgException.h",
        "include/tclap/ArgTraits.h",
        "include/tclap/CmdLine.h",
        "include/tclap/CmdLineInterface.h",
        "include/tclap/CmdLineOutput.h",
        "include/tclap/Constraint.h",
        "include/tclap/DocBookOutput.h",
        "include/tclap/HelpVisitor.h",
        "include/tclap/IgnoreRestVisitor.h",
        "include/tclap/MultiArg.h",
        "include/tclap/MultiSwitchArg.h",
        "include/tclap/OptionalUnlabeledTracker.h",
        "include/tclap/StandardTraits.h",
        "include/tclap/StdOutput.h",
        "include/tclap/SwitchArg.h",
        "include/tclap/UnlabeledMultiArg.h",
        "include/tclap/UnlabeledValueArg.h",
        "include/tclap/ValueArg.h",
        "include/tclap/ValuesConstraint.h",
        "include/tclap/VersionVisitor.h",
        "include/tclap/Visitor.h",
        "include/tclap/XorHandler.h",
        "include/tclap/ZshCompletionOutput.h",
    ],
    defines = [
        "HAVE_LONG_LONG=1",
        "HAVE_SSTREAM=1",
    ],
    includes = [
        "include",
    ],
    visibility = ["//visibility:public"],
)
"""

    native.new_http_archive(
        name = "tclap",
        url = "https://storage.googleapis.com/istio-build-deps/tclap-1.2.1.tar.gz",
        strip_prefix = "tclap-1.2.1",
        build_file_content = BUILD,
    )

def lightstep_repositories():
    BUILD = """
load("@protobuf//:protobuf.bzl", "cc_proto_library")

genrule(
    name = "envoy_carrier_pb",
    srcs = ["src/c++11/envoy/envoy_carrier.proto"],
    outs = ["lightstep/envoy_carrier.proto"],
    cmd = "cp $(SRCS) $@",
)

cc_proto_library(
    name = "envoy_carrier_proto",
    srcs = ["lightstep/envoy_carrier.proto"],
    include = ".",
    default_runtime = "@protobuf//:protobuf",
    protoc = "@protobuf//:protoc",
    visibility = ["//visibility:public"],
    deps = [
        "@protobuf//:cc_wkt_protos",
    ],
)

cc_library(
    name = "lightstep_core",
    srcs = [
        "src/c++11/impl.cc",
        "src/c++11/span.cc",
        "src/c++11/tracer.cc",
        "src/c++11/util.cc",
    ],
    hdrs = [
        "src/c++11/lightstep/envoy.h",
        "src/c++11/lightstep/impl.h",
        "src/c++11/lightstep/options.h",
        "src/c++11/lightstep/propagation.h",
        "src/c++11/lightstep/span.h",
        "src/c++11/lightstep/tracer.h",
        "src/c++11/lightstep/util.h",
        "src/c++11/lightstep/value.h",
        "src/c++11/mapbox_variant/recursive_wrapper.hpp",
        "src/c++11/mapbox_variant/variant.hpp",
    ],
    copts = [
        "-DPACKAGE_VERSION='\\"0.19\\"'",
        "-Iexternal/lightstep/src/c++11/lightstep",
        "-Iexternal/lightstep/src/c++11/mapbox_variant",
    ],
    includes = [
        "src/c++11",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_carrier_proto",
        "@lightstep_common//:collector_proto",
        "@protobuf//:protobuf",
    ],
)
"""

    COMMON_BUILD = """
load("@protobuf//:protobuf.bzl", "cc_proto_library")

genrule(
    name = "collector_pb",
    srcs = ["collector.proto"],
    outs = ["lightstep/collector.proto"],
    cmd = "cp $(SRCS) $@",
)

cc_proto_library(
    name = "collector_proto",
    srcs = ["lightstep/collector.proto"],
    include = ".",
    default_runtime = "@protobuf//:protobuf",
    protoc = "@protobuf//:protoc",
    visibility = ["//visibility:public"],
    deps = [
        "@protobuf//:cc_wkt_protos",
    ],
)
"""

    native.new_git_repository(
        name = "lightstep_common",
        remote = "https://github.com/lightstep/lightstep-tracer-common.git",
        commit = "8d932f7f76cd286691e6179621d0012b0ff1e6aa",
        build_file_content = COMMON_BUILD,
    )

    native.new_git_repository(
        name = "lightstep",
        remote = "https://github.com/lightstep/lightstep-tracer-cpp.git",
        commit = "5a71d623cac17a059041b04fabca4ed86ffff7cc",
        build_file_content = BUILD,
    )

def http_parser_repositories():
    BUILD = """
cc_library(
    name = "http_parser",
    srcs = [
        "http_parser.c",
    ],
    hdrs = [
        "http_parser.h",
    ],
    visibility = ["//visibility:public"],
)
"""

    native.new_git_repository(
        name = "http_parser",
        remote = "https://github.com/nodejs/http-parser.git",
        commit = "9b0d5b33ebdaacff1dadd06bad4e198b11ff880e",
        build_file_content = BUILD,
    )

def rapidjson_repositories():
    BUILD = """
cc_library(
    name = "rapidjson",
    srcs = glob([
        "include/rapidjson/internal/*.h",
    ]),
    hdrs = glob([
        "include/rapidjson/*.h",
        "include/rapidjson/error/*.h",
    ]),
    includes = ["include"],
    visibility = ["//visibility:public"],
)
"""

    native.new_git_repository(
        name = "rapidjson",
        remote = "https://github.com/miloyip/rapidjson.git",
        commit = "f54b0e47a08782a6131cc3d60f94d038fa6e0a51", # v1.1.0
        build_file_content = BUILD,
    )

def nghttp2_repositories():
    BUILD = """
genrule(
    name = "config",
    srcs = glob([
        "**/*",
    ]),
    outs = [
        "config.h",
    ],
    cmd = "$(location configure) --enable-lib-only --enable-shared=no" +
          " && cp config.h $@",
    tools = [
        "configure",
    ],
)

cc_library(
    name = "nghttp2",
    srcs = glob([
        "lib/*.c",
        "lib/*.h",
    ]) + ["config.h"],
    hdrs = glob([
        "lib/includes/nghttp2/*.h",
    ]),
    copts = [
        "-DHAVE_CONFIG_H",
        "-DBUILDING_NGHTTP2",
    ],
    includes = [
        ".",
        "lib/includes",
    ],
    visibility = ["//visibility:public"],
)
"""

    native.new_http_archive(
        name = "nghttp2",
        url = "https://github.com/nghttp2/nghttp2/releases/download/v1.14.1/nghttp2-1.14.1.tar.gz",
        strip_prefix = "nghttp2-1.14.1",
        build_file_content = BUILD,
    )


def envoy_dependencies():
    boringssl_repositories()
    protobuf_repositories()
    googletest_repositories()
    libevent_repositories()
    spdlog_repositories()
    tclap_repositories()
    lightstep_repositories()
    http_parser_repositories()
    rapidjson_repositories()
    nghttp2_repositories()
