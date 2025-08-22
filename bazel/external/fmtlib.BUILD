load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_license//rules:license.bzl", "license")

package(
    default_applicable_licenses = [":license"],
)

exports_files([
    "LICENSE",
])

license(
    name = "license",
    license_kinds = ["@rules_license//licenses/spdx:MIT"],
    license_text = "LICENSE",
)

cc_library(
    name = "fmt",
    srcs = [
        #"src/fmt.cc", # No C++ module support, yet in Bazel (https://github.com/bazelbuild/bazel/pull/19940)
        "src/format.cc",
        "src/os.cc",
    ],
    hdrs = glob([
        "include/fmt/*.h",
    ]),
    copts = select({
        "@rules_cc//cc/compiler:msvc-cl": ["-utf-8"],
        "//conditions:default": [],
    }),
    includes = ["include"],
    strip_include_prefix = "include",  # workaround: only needed on some macOS systems (see https://github.com/bazelbuild/bazel-central-registry/issues/1537)
    visibility = ["//visibility:public"],
)