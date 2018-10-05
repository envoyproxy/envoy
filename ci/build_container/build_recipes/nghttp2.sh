#!/bin/bash

set -e

VERSION=1.33.0
SHA256=42fff7f290100c02234ac3b0095852e4392e6bfd95ebed900ca8bd630850d88c

curl https://github.com/nghttp2/nghttp2/releases/download/v"$VERSION"/nghttp2-"$VERSION".tar.gz -sLo nghttp2-"$VERSION".tar.gz \
  && echo "$SHA256 " nghttp2-"$VERSION".tar.gz | shasum -a 256 --check
tar xf nghttp2-"$VERSION".tar.gz
cd nghttp2-"$VERSION"

# Allow nghttp2 to build as static lib on Windows
# TODO: remove once https://github.com/nghttp2/nghttp2/pull/1198 is merged
cat > nghttp2_cmakelists.diff << 'EOF'
diff --git a/lib/CMakeLists.txt b/lib/CMakeLists.txt
index 17e422b2..e58070f5 100644
--- a/lib/CMakeLists.txt
+++ b/lib/CMakeLists.txt
@@ -56,6 +56,7 @@ if(HAVE_CUNIT OR ENABLE_STATIC_LIB)
     COMPILE_FLAGS "${WARNCFLAGS}"
     VERSION ${LT_VERSION} SOVERSION ${LT_SOVERSION}
     ARCHIVE_OUTPUT_NAME nghttp2
+    ARCHIVE_OUTPUT_DIRECTORY static
   )
   target_compile_definitions(nghttp2_static PUBLIC "-DNGHTTP2_STATICLIB")
   if(ENABLE_STATIC_LIB)
EOF

if [[ "${OS}" == "Windows_NT" ]]; then
  git apply nghttp2_cmakelists.diff
fi

mkdir build
cd build

cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
  -DCMAKE_INSTALL_LIBDIR="$THIRDPARTY_BUILD/lib" \
  -DENABLE_STATIC_LIB=on \
  -DENABLE_LIB_ONLY=on \
  ..
ninja
ninja install

if [[ "${OS}" == "Windows_NT" ]]; then
  cp "lib/CMakeFiles/nghttp2_static.dir/nghttp2_static.pdb" "$THIRDPARTY_BUILD/lib/nghttp2_static.pdb"
fi
