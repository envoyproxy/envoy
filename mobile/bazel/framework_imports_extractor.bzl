"""
This rule declares outputs for files from a distributable framework.
This allows us to reproduce what it's like to import our distribution artifact
within the same build. Ideally we could just propagate the directory so we
didn't have to enumerate the files in the framework zip, but that isn't
supported by 'apple_static_framework_import'.
"""

load("//bazel:config.bzl", "MINIMUM_IOS_VERSION")

def _framework_imports_extractor(ctx):
    outputs = [
        ctx.actions.declare_file("Envoy.framework/Envoy"),
        ctx.actions.declare_file("Envoy.framework/Headers/Envoy.h"),
        ctx.actions.declare_file("Envoy.framework/Modules/module.modulemap"),
    ]
    for arch in ctx.split_attr.framework.keys():
        if not arch.startswith("ios_"):
            fail("Unexpected arch: {}".format(arch))

        arch = arch[4:]

        # ios_sim_arm64 is a temporary special case for the M1.
        if arch.startswith("sim_"):
            arch = arch[4:]

        outputs.extend([
            ctx.actions.declare_file("Envoy.framework/Modules/Envoy.swiftmodule/{}.swiftdoc".format(arch)),
            ctx.actions.declare_file("Envoy.framework/Modules/Envoy.swiftmodule/{}.swiftinterface".format(arch)),
        ])

    if len(ctx.attr.framework[0].files.to_list()) != 1:
        fail("Expected exactly one framework zip, got {}".format(ctx.attr.framework[0].files))

    framework_zip = ctx.attr.framework[0].files.to_list()[0]
    ctx.actions.run_shell(
        inputs = [framework_zip],
        outputs = outputs,
        # Workaround for https://github.com/bazelbuild/rules_apple/issues/1489
        command = "unzip -qq {} -d {} || true".format(framework_zip.path, ctx.bin_dir.path),
        progress_message = "Extracting framework",
    )

    return [DefaultInfo(files = depset(outputs))]

framework_imports_extractor = rule(
    attrs = dict(
        framework = attr.label(
            mandatory = True,
            cfg = apple_common.multi_arch_split,
        ),
        platform_type = attr.string(default = "ios"),
        minimum_os_version = attr.string(default = MINIMUM_IOS_VERSION),
    ),
    # fragments = ["apple"],
    implementation = _framework_imports_extractor,
)
