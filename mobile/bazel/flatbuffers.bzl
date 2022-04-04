load("@com_github_google_flatbuffers//:build_defs.bzl", "flatbuffer_cc_library", "flatbuffer_library_public")
load("@envoy_mobile//bazel:kotlin_lib.bzl", "envoy_mobile_kt_library")

def envoy_mobile_flatbuffers_library(name, srcs, namespace, types):
    flatbuffer_cc_library(
        name = "{}_fb_cc_lib".format(name),
        srcs = srcs,
    )

    directory_path = namespace.replace(".", "/")
    kotlin_files = [directory_path + "/" + t + ".kt" for t in types]
    flatbuffer_library_public(
        name = "{}_fb_kt_srcs".format(name),
        srcs = srcs,
        # TODO(snowp): For some reason I can't get this to work with just one output file for java. For now we can avoid dealing with this since basically all use cases of this would result in multiple files for Java.
        outs = kotlin_files,
        language_flag = "--kotlin",
    )

    envoy_mobile_kt_library(
        name = "{}_fb_kt_lib".format(name),
        srcs = [":{}_fb_kt_srcs".format(name)],
        deps = ["@maven//:com_google_flatbuffers_flatbuffers_java"],
        exports = ["@maven//:com_google_flatbuffers_flatbuffers_java"],
    )

    swift_intermediate_outputs = ["{}_generated.swift".format(f.replace(".fbs", "")) for f in srcs]
    flatbuffer_library_public(
        name = "{}_fb_swift_intermediates".format(name),
        srcs = srcs,
        outs = swift_intermediate_outputs,
        language_flag = "--swift",
    )

    # TODO: Upstream Swift generation that can use @_implementationOnly
    swift_outputs = ["{}_processed.swift".format(f.replace(".fbs", "")) for f in srcs]
    native.genrule(
        name = "{}_fb_swift_srcs".format(name),
        outs = swift_outputs,
        srcs = swift_intermediate_outputs,
        cmd = """
        sed -e 's/import FlatBuffers/@_implementationOnly import FlatBuffers/g' -e 's/public /internal /g' $(SRCS) > $(OUTS)
        """,
    )
