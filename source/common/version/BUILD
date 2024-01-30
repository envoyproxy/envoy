load(
    "//bazel:envoy_build_system.bzl",
    "envoy_basic_cc_library",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_package()

genrule(
    name = "generate_version_number",
    srcs = ["//:VERSION.txt"],
    outs = ["version_number.h"],
    cmd = """echo "#define BUILD_VERSION_NUMBER \\"$$(cat $<)\\"" >$@""",
    visibility = ["//visibility:private"],
)

genrule(
    name = "generate_api_version_number",
    srcs = ["//:API_VERSION.txt"],
    outs = ["api_version_number.h"],
    cmd = """./$(location //tools/api_versioning:generate_api_version_header_bin) $< >$@""",
    tools = ["//tools/api_versioning:generate_api_version_header_bin"],
    visibility = ["//visibility:private"],
)

genrule(
    name = "generate_version_linkstamp",
    outs = ["manual_linkstamp.cc"],
    cmd = select({
        # Only iOS builds typically follow this logic, OS/X is built as a normal binary
        "//bazel:apple": "$(location :generate_version_linkstamp.sh) Library >> $@",
        "//conditions:default": "$(location :generate_version_linkstamp.sh) >> $@",
    }),
    # Undocumented attr to depend on workspace status files.
    # https://github.com/bazelbuild/bazel/issues/4942
    # Used here because generate_version_linkstamp.sh depends on the workspace status files.
    stamp = 1,
    tools = [":generate_version_linkstamp.sh"],
    visibility = ["//visibility:private"],
)

envoy_cc_library(
    name = "version_includes",
    hdrs = [
        "version.h",
        ":generate_version_number",
    ],
    deps = [
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "version_lib",
    srcs = ["version.cc"],
    copts = envoy_select_boringssl(
        [
            "-DENVOY_SSL_VERSION=\\\"BoringSSL-FIPS\\\"",
            "-DENVOY_SSL_FIPS",
        ],
        ["-DENVOY_SSL_VERSION=\\\"BoringSSL\\\""],
    ),
    external_deps = ["ssl"],
    tags = ["notidy"],
    deps = [
        ":version_includes",
        "//source/common/common:macros",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "api_version_includes",
    hdrs = [
        "api_version_struct.h",
        ":generate_api_version_number",
    ],
)

envoy_cc_library(
    name = "api_version_lib",
    hdrs = ["api_version.h"],
    deps = [":api_version_includes"],
)

envoy_basic_cc_library(
    name = "manual_version_linkstamp",
    srcs = [":generate_version_linkstamp"],
    visibility = ["//visibility:private"],
)

envoy_basic_cc_library(
    name = "version_linkstamp",
    linkstamp = select({
        "//bazel:manual_stamp": None,
        "//conditions:default": "version_linkstamp.cc",
    }),
    # Linking this library makes build cache inefficient, limiting this to //source/exe package only.
    # Tests are linked with //test/test_common:test_version_linkstamp.
    visibility = ["//source/exe:__pkg__"],
    deps = select({
        "//bazel:manual_stamp": [":manual_version_linkstamp"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)
