bazel --bazelrc=windows\.bazelrc test @envoy_api//test/build/...
bazel --bazelrc=windows\.bazelrc build @boringssl//:ssl
bazel --bazelrc=windows\.bazelrc build //external:ares
bazel --bazelrc=windows\.bazelrc build //external:event
bazel --bazelrc=windows\.bazelrc build //external:yaml_cpp
bazel --bazelrc=windows\.bazelrc build //external:zlib
