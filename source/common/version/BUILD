load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_boringssl",
)

licenses(["notice"])  # Apache 2

envoy_package()

genrule(
    name = "generate_version_number",
    srcs = ["//:VERSION"],
    outs = ["version_number.h"],
    cmd = """echo "#define BUILD_VERSION_NUMBER \\"$$(cat $<)\\"" >$@""",
)

genrule(
    name = "generate_version_linkstamp",
    outs = ["lib/version_linkstamp.h"],
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
)

genrule(
    name = "generate_version_linkstamp_empty",
    outs = ["empty/version_linkstamp.h"],
    cmd = """>$@""",
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
    hdrs = select({
        "//bazel:manual_stamp": [":generate_version_linkstamp"],
        # By default the header file is empty.
        # This is done so that the definitions linked via the linkstamp rule don't cause collisions.
        "//conditions:default": [":generate_version_linkstamp_empty"],
    }),
    copts = envoy_select_boringssl(
        ["-DENVOY_SSL_VERSION=\\\"BoringSSL-FIPS\\\""],
        ["-DENVOY_SSL_VERSION=\\\"BoringSSL\\\""],
    ),
    linkstamp = "version_linkstamp.cc",
    strip_include_prefix = select({
        "//bazel:manual_stamp": "lib",
        "//conditions:default": "empty",
    }),
    deps = [
        ":version_includes",
        "//source/common/common:macros",
        "//source/common/protobuf:utility_lib",
    ],
)
