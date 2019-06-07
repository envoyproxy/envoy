Analysis of Binary Size
-----------------------

### Creating a test binary

Investigations into the top file size contributions of the Envoy library have
been performed via a simple test program that imports the `main_interface.h`
file:

```
#include "main_interface.h"

using namespace std;

int main() {
  return 0;
}
```

One will then need to create the binary via bazel build using a patch similar
to:
```
diff --git a/library/common/BUILD b/library/common/BUILD
index cbf681c..e2162af 100644
--- a/library/common/BUILD
+++ b/library/common/BUILD
@@ -1,6 +1,6 @@
 licenses(["notice"])  # Apache 2

-load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")
+load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package", "envoy_cc_binary")

 envoy_package()

@@ -11,3 +11,10 @@ envoy_cc_library(
     repository = "@envoy",
     deps = ["@envoy//source/exe:envoy_main_common_lib"]
 )
+
+envoy_cc_binary(
+    name = "test_binary",
+    srcs = ["test_binary.cc"],
+    repository = "@envoy",
+    deps = [":envoy_main_interface_lib"]
+)
```

When building the binary, using the following flags in the .bazelrc would yield a binary with
the correct compiler optimizations for the purposes of the investigation:

```
build:tinybuild --copt="-Os"
build:tinybuild --define google_grpc=disabled
build:tinybuild --define signal_trace=disabled
build:tinybuild --define tcmalloc=disabled
build:tinybuild --define disable_hot_restart=enabled
```

One can build both an unstripped (for the debug symbols) and a stripped binary (for analysis) via:

```
bazel build //library/common:test_binary --config=tinybuild --copt="-g"

- or -

bazel build //library/common:test_binary.stripped --config=tinybuild --copt="-g"
```

### Performing analysis

[Bloaty](https://github.com/google/bloaty) has been more useful than `objdump`
when performing the investigation. The main command used to generate reports is
the following from the root of the build directory:

```
bloaty -w \
       -s file \
       -n 0 \
       --debug-file=bazel-bin/library/common/test_binary \
       -d compileunits \
       bazel-bin/library/common/test_binary.stripped
```

For a more detailed breakdown, one can substitute `-d compileunits,inlined`.
