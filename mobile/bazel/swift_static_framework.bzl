"""
This rules creates a fat static framework that can be included later with
static_framework_import
"""

load("@build_bazel_apple_support//lib:apple_support.bzl", "apple_support")
load("@build_bazel_rules_swift//swift:swift.bzl", "SwiftInfo", "swift_library")
load("@build_bazel_rules_apple//apple/internal:transition_support.bzl", "transition_support")

MINIMUM_IOS_VERSION = "11.0"

_PLATFORM_TO_SWIFTMODULE = {
    "ios_armv7": "arm",
    "ios_arm64": "arm64",
    "ios_i386": "i386",
    "ios_x86_64": "x86_64",
}

def _zip_binary_arg(module_name, input_file):
    return "{module_name}.framework/{module_name}={file_path}".format(
        module_name = module_name,
        file_path = input_file.path,
    )

def _zip_header_arg(module_name, input_file):
    return "{module_name}.framework/Headers/{module_name}-Swift.h={file_path}".format(
        module_name = module_name,
        file_path = input_file.path,
    )

def _zip_swift_arg(module_name, swift_identifier, input_file):
    return "{module_name}.framework/Modules/{module_name}.swiftmodule/{swift_identifier}.{ext}={file_path}".format(
        module_name = module_name,
        swift_identifier = swift_identifier,
        ext = input_file.extension,
        file_path = input_file.path,
    )

def _swift_static_framework_impl(ctx):
    module_name = ctx.attr.framework_name
    fat_file = ctx.outputs.fat_file

    input_archives = []
    input_modules_docs = []
    zip_args = [_zip_binary_arg(module_name, fat_file)]

    for platform, archive in ctx.split_attr.archive.items():
        swiftmodule_identifier = _PLATFORM_TO_SWIFTMODULE[platform]
        if not swiftmodule_identifier:
            fail("Unhandled platform '{}'".format(platform))

        swift_info = archive[SwiftInfo]

        # We can potentially simplify this if this change lands upstream:
        # https://github.com/bazelbuild/rules_swift/issues/291
        objc_headers = [
            header
            for header in archive[apple_common.Objc].header.to_list()
            if header.path.endswith("-Swift.h")
        ]

        if len(objc_headers) == 1:
            zip_args.append(_zip_header_arg(module_name, objc_headers[0]))
        else:
            header_names = [header.basename for header in objc_headers]
            fail("Expected exactly 1 '-Swift.h' header, got {}".format(", ".join(header_names)))

        swiftdoc = swift_info.direct_swiftdocs[0]
        swiftmodule = swift_info.direct_swiftmodules[0]
        swiftinterfaces = swift_info.transitive_swiftinterfaces.to_list()
        if len(swiftinterfaces) != 1:
            fail("Expected a single swiftinterface file, got: {}".format(swiftinterfaces))
        swiftinterface = swiftinterfaces[0]

        libraries = archive[CcInfo].linking_context.libraries_to_link
        archives = []
        for library in libraries.to_list():
            archive = library.pic_static_library or library.static_library
            if archive:
                archives.append(archive)
            else:
                fail("All linked dependencies must be static")

        platform_archive = ctx.actions.declare_file("{}.{}.a".format(module_name, platform))

        libtool_args = ["-no_warning_for_no_symbols", "-static", "-syslibroot", "__BAZEL_XCODE_SDKROOT__", "-o", platform_archive.path] + [x.path for x in archives]
        apple_support.run(
            ctx,
            inputs = archives,
            outputs = [platform_archive],
            mnemonic = "LibtoolLinkedLibraries",
            progress_message = "Combining libraries for {} on {}".format(module_name, platform),
            executable = ctx.executable._libtool,
            arguments = libtool_args,
        )

        input_archives.append(platform_archive)

        input_modules_docs += [swiftdoc, swiftmodule, swiftinterface]
        zip_args += [
            _zip_swift_arg(module_name, swiftmodule_identifier, swiftdoc),
            _zip_swift_arg(module_name, swiftmodule_identifier, swiftmodule),
            _zip_swift_arg(module_name, swiftmodule_identifier, swiftinterface),
        ]

    ctx.actions.run(
        inputs = input_archives,
        outputs = [fat_file],
        mnemonic = "LipoPlatformLibraries",
        progress_message = "Creating fat library for {}".format(module_name),
        executable = "lipo",
        arguments = ["-create", "-output", fat_file.path] + [x.path for x in input_archives],
    )

    output_file = ctx.outputs.output_file
    ctx.actions.run(
        inputs = input_modules_docs + [fat_file],
        outputs = [output_file],
        mnemonic = "CreateFrameworkZip",
        progress_message = "Creating framework zip for {}".format(module_name),
        executable = ctx.executable._zipper,
        arguments = ["c", output_file.path] + zip_args,
    )

    return [
        DefaultInfo(
            files = depset([output_file]),
        ),
    ]

_swift_static_framework = rule(
    attrs = dict(
        apple_support.action_required_attrs(),
        _libtool = attr.label(
            default = "@bazel_tools//tools/objc:libtool",
            cfg = "host",
            executable = True,
        ),
        _allowlist_function_transition = attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
        _zipper = attr.label(
            default = "@bazel_tools//tools/zip:zipper",
            cfg = "host",
            executable = True,
        ),
        archive = attr.label(
            mandatory = True,
            providers = [
                CcInfo,
                SwiftInfo,
            ],
            cfg = apple_common.multi_arch_split,
        ),
        framework_name = attr.string(mandatory = True),
        minimum_os_version = attr.string(default = MINIMUM_IOS_VERSION),
        platform_type = attr.string(
            default = str(apple_common.platform_type.ios),
        ),
    ),
    cfg = transition_support.static_framework_transition,
    fragments = [
        "apple",
    ],
    outputs = {
        "fat_file": "%{framework_name}.fat",
        "output_file": "%{framework_name}.zip",
    },
    implementation = _swift_static_framework_impl,
)

def swift_static_framework(
        name,
        module_name = None,
        srcs = [],
        private_deps = [],
        copts = [],
        visibility = []):
    """Create a static library, and static framework target for a swift module

    Args:
        name: The name of the module, the framework's name will be this name
            appending Framework so you can depend on this from other modules
        srcs: Custom source paths for the swift files
        copts: Any custom swiftc opts passed through to the swift_library
        private_deps: Any deps the swift_library requires. They must be imported
            with @_implementationOnly and not exposed publicly.
    """
    archive_name = name + "_archive"
    module_name = module_name or name + "_framework"
    swift_library(
        name = archive_name,
        srcs = srcs,
        copts = copts,
        module_name = module_name,
        private_deps = private_deps,
        visibility = ["//visibility:public"],
        features = ["swift.enable_library_evolution"],
    )

    _swift_static_framework(
        name = name,
        archive = archive_name,
        framework_name = module_name,
        visibility = visibility,
    )
