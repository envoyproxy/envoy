load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_jvm_library")

# This is the magic function which helps get the name of the native library
# from the native dependency. In general, the bazel cc_binary rules will
# output a binary based on the target name. This macro just infers the output
# so file name.
#
# The main functionality of this method is used for integration java/android
# testing. Bazel itself doesn't play well with different genrules outputting
# the same output. In this project there's 3 types of artifacts we end up
# using: envoy's aar, integration tests with just vanilla jvm, integration
# tests with android. Each of these require a different so file built which
# means that Bazel will have to output 3 types of so files with different names.
def native_lib_name(native_dep):
    lib_name = ""
    if ":" in native_dep:
        lib_name = native_dep.split(":")[1].split(".so")[0]
    else:
        lib_name = native_dep.split(".so")[0]
    return lib_name

def envoy_mobile_kt_library(name, visibility = None, srcs = [], deps = []):
    # These source files must be re-exported to the kotlin custom library rule to ensure their
    # inclusion. This is used to work around testing visibility.
    native.filegroup(
        name = name + "_srcs",
        srcs = srcs,
        visibility = visibility,
    )

    kt_jvm_library(
        name = name,
        srcs = srcs,
        deps = deps,
        visibility = visibility,
    )

# Basic macro which uses a genrule to generate a jnilib file from an so file
#
# The native dep passed in should be the cc_binary target of the name
# lib{}.so since linux platforms expect the .so file to be named in that form.
# The resulting output will be lib{}.jnilib for the OS X platforms.
#
# Example usage:
# envoy_mobile_so_to_jni_lib(
#     name = "java_jni_lib.jnilib",
#     native_dep = "libjava_jni_lib.so",
# )
def envoy_mobile_so_to_jni_lib(name, native_dep):
    lib_name = native_lib_name(native_dep)
    output = "{}.jnilib".format(lib_name)

    return native.genrule(
        name = name,
        outs = [output],
        srcs = [native_dep],
        cmd = """
        so_file="{}.so"
        if [ ! -f $$so_file ]; then
            dir=$$(dirname $@)
            cp $< $$dir/{}.so 2>/dev/null || :
            chmod 755 $$dir/{}.so
        fi

        cp $< $@
        chmod 755 $@
        """.replace("{}", lib_name),
    )
