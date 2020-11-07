def _default_envoy_dev_impl(ctxt):
    if "LLVM_CONFIG" in ctxt.os.environ:
        ctxt.file("WORKSPACE", "")
        ctxt.file("BUILD.bazel", "")
        ctxt.symlink(ctxt.path(ctxt.attr.envoy_root).dirname.get_child("tools").get_child("clang_tools"), "clang_tools")

_default_envoy_dev = repository_rule(
    implementation = _default_envoy_dev_impl,
    attrs = {
        "envoy_root": attr.label(default = "@envoy//:BUILD"),
    },
)

def _clang_tools_impl(ctxt):
    if "LLVM_CONFIG" in ctxt.os.environ:
        llvm_config_path = ctxt.os.environ["LLVM_CONFIG"]
        exec_result = ctxt.execute([llvm_config_path, "--includedir"])
        if exec_result.return_code != 0:
            fail(llvm_config_path + " --includedir returned %d" % exec_result.return_code)
        clang_tools_include_path = exec_result.stdout.rstrip()
        exec_result = ctxt.execute([llvm_config_path, "--libdir"])
        if exec_result.return_code != 0:
            fail(llvm_config_path + " --libdir returned %d" % exec_result.return_code)
        clang_tools_lib_path = exec_result.stdout.rstrip()
        for include_dir in ["clang", "clang-c", "llvm", "llvm-c"]:
            ctxt.symlink(clang_tools_include_path + "/" + include_dir, include_dir)
        ctxt.symlink(clang_tools_lib_path, "lib")
        ctxt.symlink(Label("@envoy_dev//clang_tools/support:BUILD.prebuilt"), "BUILD")

_clang_tools = repository_rule(
    implementation = _clang_tools_impl,
    environ = ["LLVM_CONFIG"],
)

def envoy_dev_binding():
    # Treat the Envoy developer tools that require llvm as an external repo, this avoids
    # breaking bazel build //... when llvm is not installed.
    if "envoy_dev" not in native.existing_rules().keys():
        _default_envoy_dev(name = "envoy_dev")
        _clang_tools(name = "clang_tools")
