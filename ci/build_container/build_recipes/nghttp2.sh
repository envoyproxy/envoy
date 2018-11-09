#!/bin/bash

set -e

VERSION=1.34.0
SHA256=8889399ddd38aa0405f6e84f1c050a292286089441686b8a9c5e937de4f5b61d

curl https://github.com/nghttp2/nghttp2/releases/download/v"$VERSION"/nghttp2-"$VERSION".tar.gz -sLo nghttp2-"$VERSION".tar.gz \
  && echo "$SHA256" nghttp2-"$VERSION".tar.gz | sha256sum --check
tar xf nghttp2-"$VERSION".tar.gz
cd nghttp2-"$VERSION"

# Allow nghttp2 to build as static lib on Windows
# TODO: remove once https://github.com/nghttp2/nghttp2/pull/1198 is merged
cat > nghttp2_cmakelists.diff << 'EOF'
diff --git a/lib/CMakeLists.txt b/lib/CMakeLists.txt
index 17e422b..54de0b8 100644
--- a/lib/CMakeLists.txt
+++ b/lib/CMakeLists.txt
@@ -55,7 +55,7 @@ if(HAVE_CUNIT OR ENABLE_STATIC_LIB)
   set_target_properties(nghttp2_static PROPERTIES
     COMPILE_FLAGS "${WARNCFLAGS}"
     VERSION ${LT_VERSION} SOVERSION ${LT_SOVERSION}
-    ARCHIVE_OUTPUT_NAME nghttp2
+    ARCHIVE_OUTPUT_NAME nghttp2_static
   )
   target_compile_definitions(nghttp2_static PUBLIC "-DNGHTTP2_STATICLIB")
   if(ENABLE_STATIC_LIB)
EOF

if [[ "${OS}" == "Windows_NT" ]]; then
  git apply nghttp2_cmakelists.diff
fi

mkdir build
cd build

build_type=Release
if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # On Windows, every object file in the final executable needs to be compiled to use the
  # same version of the C Runtime Library -- there are different versions for debug and
  # release builds. The script "ci/do_ci.ps1" will pass BAZEL_WINDOWS_BUILD_TYPE=dbg
  # to bazel when performing a debug build.
  build_type=Debug
fi

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DCMAKE_INSTALL_LIBDIR="$THIRDPARTY_BUILD/lib" \
  -DENABLE_STATIC_LIB=on \
  -DENABLE_LIB_ONLY=on \
  -DCMAKE_BUILD_TYPE="$build_type" \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" && "${BAZEL_WINDOWS_BUILD_TYPE}" == "dbg" ]]; then
  # .pdb files are not generated for release builds
  cp "lib/CMakeFiles/nghttp2_static.dir/nghttp2_static.pdb" "$THIRDPARTY_BUILD/lib/nghttp2_static.pdb"
fi
