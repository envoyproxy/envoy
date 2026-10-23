GCC builds (``--config=gcc``) now use a hermetic GCC 13.4.0 toolchain with static libstdc++ and
Envoy's hermetic sysroot (the same as Clang). Select another prebuilt version with
``--@gcc_toolchain//toolchain:gcc_version=<version>``, or use the root module's
``envoy_gcc_extension.host(path = "...")`` override for a system GCC installation.
