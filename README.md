This is a fork of envoy with cpp sdk for dynamic modules.

To build,
bazel build -c dbg --compilation_mode=dbg  -s --verbose_failures --sandbox_debug --noincompatible_sandbox_hermetic_tmp --repo_env=CC=clang --jobs 1 envoy

 bazel build --verbose_failures --compilation_mode=dbg -c dbg --sandbox_debug --noincompatible_sandbox_hermetic_tmp --repo_env=CC=clang --jobs 1 //source/extensions/dynamic_modules/sdk/cpp:envoy_cpp_sdk


 To test,
 bazel test --verbose_failures --sandbox_debug --noincompatible_sandbox_hermetic_tmp --repo_env=CC=clang --jobs 1 --test_output=all --enable_workspace //test/extensions/dynamic_modules:cpp_sdk_test