# The build rules below for external dependencies build rules are maintained on a best effort basis.
# The rules are provided for developer convenience. For production builds, we recommend building the
# libraries according to their canonical build systems and expressing the dependencies in a manner
# similar to ci/WORKSPACE.

load(
    ":versions.bzl",
    "BORINGSSL_COMMIT",
    "BORINGSSL_REMOTE",
    "CARES_COMMIT",
    "CARES_REMOTE",
    "GOOGLETEST_COMMIT",
    "GOOGLETEST_REMOTE",
    "HTTP_PARSER_COMMIT",
    "HTTP_PARSER_REMOTE",
    "LIBEVENT_HTTP_ARCHIVE",
    "LIBEVENT_PREFIX",
    "LIGHTSTEP_HTTP_ARCHIVE",
    "LIGHTSTEP_PREFIX",
    "NGHTTP2_HTTP_ARCHIVE",
    "NGHTTP2_PREFIX",
    "PROTOBUF_COMMIT",
    "PROTOBUF_REMOTE",
    "RAPIDJSON_COMMIT",
    "RAPIDJSON_REMOTE",
    "SPDLOG_COMMIT",
    "SPDLOG_REMOTE",
    "TCLAP_HTTP_ARCHIVE",
    "TCLAP_PREFIX",
)

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

genrule(
    name = "config",
    # This list must not contain ares_config.h.
    srcs = glob(["**/*.m4"]),
    outs = ["ares_config.h"],
    # There is some serious evil here. buildconf needs to be invoked in its source location, where
    # it also generates temp files (no option to place in TMPDIR). Also, configure does a cd
    # relative dance that doesn't play nice with the symlink execroot in the Bazel build. So,
    # disable sandboxing and do some fragile stuff with the build dirs.
    # TODO(htuch): Figure out a cleaner way to handle this.
    cmd = "pushd ../../external/cares_git; ./buildconf; ./configure; " +
          "cp -f ares_config.h ../../execroot/envoy/$@",
    local = 1,
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
        remote = CARES_REMOTE,
        commit = CARES_COMMIT,
        build_file_content = BUILD,
    )

def boringssl_repositories():
    native.git_repository(
        name = "boringssl",
        remote = BORINGSSL_REMOTE,
        commit = BORINGSSL_COMMIT,
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
        remote = GOOGLETEST_REMOTE,
        commit = GOOGLETEST_COMMIT,
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
        remote = HTTP_PARSER_REMOTE,
        commit = HTTP_PARSER_COMMIT,
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
        url = LIBEVENT_HTTP_ARCHIVE,
        strip_prefix = LIBEVENT_PREFIX,
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
        "-Iexternal/lightstep_tar/src/c++11/lightstep",
        "-Iexternal/lightstep_tar/src/c++11/mapbox_variant",
    ],
    includes = ["src/c++11"],
    visibility = ["//visibility:public"],
    deps = [
        ":collector_proto",
        ":lightstep_carrier_proto",
        "//external:protobuf",
    ],
)

cc_proto_library(
    name = "collector_proto",
    srcs = ["lightstep-tracer-common/collector.proto"],
    include = "lightstep-tracer-common",
    deps = [
        "//external:cc_wkt_protos",
    ],
    protoc = "//external:protoc",
    default_runtime = "//external:protobuf",
)

cc_proto_library(
    name = "lightstep_carrier_proto",
    srcs = ["lightstep-tracer-common/lightstep_carrier.proto"],
    include = "lightstep-tracer-common",
    deps = [
        "//external:cc_wkt_protos",
    ],
    protoc = "//external:protoc",
    default_runtime = "//external:protobuf",
)
"""

    native.new_http_archive(
        name = "lightstep_tar",
        url = LIGHTSTEP_HTTP_ARCHIVE,
        strip_prefix = LIGHTSTEP_PREFIX,
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
        url = NGHTTP2_HTTP_ARCHIVE,
        strip_prefix = NGHTTP2_PREFIX,
        build_file_content = BUILD,
    )

def protobuf_repositories():
    native.git_repository(
        name = "protobuf_git",
        remote = PROTOBUF_REMOTE,
        commit = PROTOBUF_COMMIT,
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
        remote = RAPIDJSON_REMOTE,
        commit = RAPIDJSON_COMMIT,
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
        remote = SPDLOG_REMOTE,
        commit = SPDLOG_COMMIT,
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
        url = TCLAP_HTTP_ARCHIVE,
        strip_prefix = TCLAP_PREFIX,
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
