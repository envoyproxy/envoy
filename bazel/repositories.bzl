# The build rules below for external dependencies build rules are maintained on a best effort basis.
# The rules are provided for developer convenience. For production builds, we recommend building the
# libraries according to their canonical build systems and expressing the dependencies in a manner
# similar to ci/WORKSPACE.

def ares_repositories():
    BUILD = """
cc_library(
    name = "ares",
    srcs = [
        "ares__close_sockets.c",
        "ares__get_hostent.c",
        "ares__read_line.c",
        "ares__timeval.c",
        "ares_cancel.c",
        "ares_create_query.c",
        "ares_data.c",
        "ares_destroy.c",
        "ares_expand_name.c",
        "ares_expand_string.c",
        "ares_fds.c",
        "ares_free_hostent.c",
        "ares_free_string.c",
        "ares_getenv.c",
        "ares_gethostbyaddr.c",
        "ares_gethostbyname.c",
        "ares_getnameinfo.c",
        "ares_getopt.c",
        "ares_getsock.c",
        "ares_init.c",
        "ares_library_init.c",
        "ares_llist.c",
        "ares_mkquery.c",
        "ares_nowarn.c",
        "ares_options.c",
        "ares_parse_a_reply.c",
        "ares_parse_aaaa_reply.c",
        "ares_parse_mx_reply.c",
        "ares_parse_naptr_reply.c",
        "ares_parse_ns_reply.c",
        "ares_parse_ptr_reply.c",
        "ares_parse_soa_reply.c",
        "ares_parse_srv_reply.c",
        "ares_parse_txt_reply.c",
        "ares_platform.c",
        "ares_process.c",
        "ares_query.c",
        "ares_search.c",
        "ares_send.c",
        "ares_strcasecmp.c",
        "ares_strdup.c",
        "ares_strerror.c",
        "ares_timeout.c",
        "ares_version.c",
        "ares_writev.c",
        "bitncmp.c",
        "inet_net_pton.c",
        "inet_ntop.c",
        "windows_port.c",
    ],
    hdrs = [
        "ares.h",
        "ares_build.h",
        "ares_config.h",
        "ares_data.h",
        "ares_dns.h",
        "ares_getenv.h",
        "ares_getopt.h",
        "ares_inet_net_pton.h",
        "ares_iphlpapi.h",
        "ares_ipv6.h",
        "ares_library_init.h",
        "ares_llist.h",
        "ares_nowarn.h",
        "ares_platform.h",
        "ares_private.h",
        "ares_rules.h",
        "ares_setup.h",
        "ares_strcasecmp.h",
        "ares_strdup.h",
        "ares_version.h",
        "ares_writev.h",
        "bitncmp.h",
        "nameser.h",
        "setup_once.h",
    ],
    copts = [
        "-DHAVE_CONFIG_H",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
)

# There is some moderate evil here. buildconf needs to be invoked in its source
# location, where it also generates temp files (no option to place in TMPDIR).
# Also, configure does a cd relative dance that doesn't play nice with the
# symlink execroot in the Bazel build. So, copy everything prior to building.
ARES_CONFIG_SH_CMDS = [
    "export TMPDIR=$$(realpath $(@D))",
    "rm -rf $(@D)/cares_build",
    "mkdir -p $(@D)/cares_build",
    "rsync -aRLv $(SRCS) $(@D)/cares_build",
    "pushd $(@D)/cares_build/external/cares_git",
    "./buildconf",
    "./configure",
    "popd",
    "cp $(@D)/cares_build/external/cares_git/ares_config.h $@",
]

genrule(
    name = "config",
    # This list must not contain ares_config.h.
    srcs = glob(["**/*"]),
    outs = ["ares_config.h"],
    cmd = "; ".join(ARES_CONFIG_SH_CMDS),
)

genrule(
    name = "ares_build",
    srcs = ["ares_build.h.dist"],
    outs = ["ares_build.h"],
    cmd = "cp $(SRCS) $@",
)
"""

    native.new_git_repository(
        name = "cares_git",
        remote = "https://github.com/c-ares/c-ares.git",
        commit = "7691f773af79bf75a62d1863fd0f13ebf9dc51b1", # v1.12.0
        build_file_content = BUILD,
    )

def boringssl_repositories():
    native.git_repository(
        name = "boringssl",
        commit = "bfd36df3da38dbf8828e712f42fbab2a0034bc40",  # 2017-02-02
        remote = "https://boringssl.googlesource.com/boringssl",
    )

def googletest_repositories():
    BUILD = """
cc_library(
    name = "googletest",
    srcs = [
        "googlemock/src/gmock-all.cc",
        "googletest/src/gtest-all.cc",
    ],
    hdrs = glob([
        "googlemock/include/**/*.h",
        "googlemock/src/*.cc",
        "googletest/include/**/*.h",
        "googletest/src/*.cc",
        "googletest/src/*.h",
    ]),
    includes = [
        "googlemock",
        "googlemock/include",
        "googletest",
        "googletest/include",
    ],
    visibility = ["//visibility:public"],
)
"""
    native.new_git_repository(
        name = "googletest_git",
        build_file_content = BUILD,
        # v1.8.0 release
        commit = "ec44c6c1675c25b9827aacd08c02433cccde7780",
        remote = "https://github.com/google/googletest.git",
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
        name = "http_parser_git",
        remote = "https://github.com/nodejs/http-parser.git",
        commit = "9b0d5b33ebdaacff1dadd06bad4e198b11ff880e",
        build_file_content = BUILD,
    )

def libevent_repositories():
    BUILD = """
genrule(
    name = "config",
    srcs = glob(["**/*"]),
    outs = ["config.h"],
    cmd = "TMPDIR=$(@D) $(location configure) --enable-shared=no --disable-libevent-regress " +
          "--disable-openssl && cp config.h $@",
    tools = ["configure"],
)

genrule(
    name = "event-config",
    srcs = [
        "config.h",
        "make-event-config.sed",
    ],
    outs = ["include/event2/event-config.h"],
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
    "evutil_time.c",
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
        "evthread-internal.h",
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
    deps = [":event"],
)"""

    native.new_http_archive(
        name = "libevent_git",
        url = "https://github.com/libevent/libevent/releases/download/release-2.1.8-stable/libevent-2.1.8-stable.tar.gz",
        strip_prefix = "libevent-2.1.8-stable",
        build_file_content = BUILD,
    )

def lightstep_repositories():
    BUILD = """
load("@protobuf_git//:protobuf.bzl", "cc_proto_library")

cc_library(
    name = "lightstep_core",
    srcs = [
        "src/c++11/impl.cc",
        "src/c++11/span.cc",
        "src/c++11/tracer.cc",
        "src/c++11/util.cc",
    ],
    hdrs = [
        "src/c++11/lightstep/impl.h",
        "src/c++11/lightstep/options.h",
        "src/c++11/lightstep/propagation.h",
        "src/c++11/lightstep/carrier.h",
        "src/c++11/lightstep/span.h",
        "src/c++11/lightstep/tracer.h",
        "src/c++11/lightstep/util.h",
        "src/c++11/lightstep/value.h",
        "src/c++11/mapbox_variant/recursive_wrapper.hpp",
        "src/c++11/mapbox_variant/variant.hpp",
    ],
    copts = [
        "-DPACKAGE_VERSION='\\"0.36\\"'",
        "-Iexternal/lightstep_git/src/c++11/lightstep",
        "-Iexternal/lightstep_git/src/c++11/mapbox_variant",
    ],
    includes = ["src/c++11"],
    visibility = ["//visibility:public"],
    deps = [
        "@lightstep_common_git//:collector_proto",
        "@lightstep_common_git//:lightstep_carrier_proto",
        "//external:protobuf",
    ],
)"""

    COMMON_BUILD = """
load("@protobuf_git//:protobuf.bzl", "cc_proto_library")

cc_proto_library(
    name = "collector_proto",
    srcs = ["collector.proto"],
    include = ".",
    deps = [
        "//external:cc_wkt_protos",
    ],
    protoc = "//external:protoc",
    default_runtime = "//external:protobuf",
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "lightstep_carrier_proto",
    srcs = ["lightstep_carrier.proto"],
    include = ".",
    deps = [
        "//external:cc_wkt_protos",
    ],
    protoc = "//external:protoc",
    default_runtime = "//external:protobuf",
    visibility = ["//visibility:public"],
)
"""

    native.new_git_repository(
        name = "lightstep_common_git",
        remote = "https://github.com/lightstep/lightstep-tracer-common.git",
        commit = "cbbecd671c1ae1f20ae873c5da688c8c14d04ec3",
        build_file_content = COMMON_BUILD,
    )

    native.new_git_repository(
        name = "lightstep_git",
        remote = "https://github.com/lightstep/lightstep-tracer-cpp.git",
        commit = "f1dc8f3dfd529350e053fd21273e627f409ae428", # 0.36
        build_file_content = BUILD,
    )

def nghttp2_repositories():
    BUILD = """
genrule(
    name = "config",
    srcs = glob(["**/*"]),
    outs = ["config.h"],
    cmd = "TMPDIR=$(@D) $(location configure) --enable-lib-only --enable-shared=no" +
          " && cp config.h $@",
    tools = ["configure"],
)

cc_library(
    name = "nghttp2",
    srcs = glob([
        "lib/*.c",
        "lib/*.h",
    ]) + ["config.h"],
    hdrs = glob(["lib/includes/nghttp2/*.h"]),
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
        name = "nghttp2_tar",
        url = "https://github.com/nghttp2/nghttp2/releases/download/v1.20.0/nghttp2-1.20.0.tar.gz",
        strip_prefix = "nghttp2-1.20.0",
        build_file_content = BUILD,
    )

def protobuf_repositories():
    native.git_repository(
        name = "protobuf_git",
        # Using a non-canonical repository/branch here. This is a workaround to the lack of
        # merge on https://github.com/google/protobuf/pull/2508, which is needed for supporting
        # arbitrary CC compiler locations from the environment. The branch is
        # https://github.com/htuch/protobuf/tree/v3.2.0-default-shell-env, which is the 3.2.0
        # release with the above mentioned PR cherry picked.
        commit = "d490587268931da78c942a6372ef57bb53db80da",
        remote = "https://github.com/htuch/protobuf.git",
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
        name = "rapidjson_git",
        remote = "https://github.com/miloyip/rapidjson.git",
        commit = "f54b0e47a08782a6131cc3d60f94d038fa6e0a51", # v1.1.0
        build_file_content = BUILD,
    )

def spdlog_repositories():
    BUILD = """
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "spdlog",
    hdrs = glob([
        "include/**/*.cc",
        "include/**/*.h",
    ]),
    strip_include_prefix = "include",
)
"""

    native.new_git_repository(
        name = "spdlog_git",
        build_file_content = BUILD,
        # v0.11.0 release
        commit = "1f1f6a5f3b424203a429e9cb78e6548037adefa8",
        remote = "https://github.com/gabime/spdlog.git",
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
    includes = ["include"],
    visibility = ["//visibility:public"],
)
"""
    native.new_http_archive(
        name = "tclap_archive",
        url = "https://storage.googleapis.com/istio-build-deps/tclap-1.2.1.tar.gz",
        strip_prefix = "tclap-1.2.1",
        build_file_content = BUILD,
    )

def envoy_dependencies():
    ares_repositories()
    boringssl_repositories()
    googletest_repositories()
    http_parser_repositories()
    libevent_repositories()
    lightstep_repositories()
    nghttp2_repositories()
    protobuf_repositories()
    rapidjson_repositories()
    spdlog_repositories()
    tclap_repositories()
