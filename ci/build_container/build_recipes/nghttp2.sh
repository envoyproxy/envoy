#!/bin/bash

set -e

VERSION=1.32.0

curl https://github.com/nghttp2/nghttp2/releases/download/v"$VERSION"/nghttp2-"$VERSION".tar.gz -sLo nghttp2-"$VERSION".tar.gz
tar xf nghttp2-"$VERSION".tar.gz
cd nghttp2-"$VERSION"

# Allow nghttp2 to build as static lib on Windows
cat > nghttp2_cmakelists.diff << 'EOF'
diff --git a/lib/CMakeLists.txt b/lib/CMakeLists.txt
index 17e422b..b2e7a6e 100644
--- a/lib/CMakeLists.txt
+++ b/lib/CMakeLists.txt
@@ -37,8 +37,8 @@ if(WIN32)
   set(NGHTTP2_RES ${CMAKE_CURRENT_BINARY_DIR}/version.rc)
 endif()

-# Public shared library
-add_library(nghttp2 SHARED ${NGHTTP2_SOURCES} ${NGHTTP2_RES})
+# Public library
+add_library(nghttp2 ${NGHTTP2_SOURCES} ${NGHTTP2_RES})
 set_target_properties(nghttp2 PROPERTIES
   COMPILE_FLAGS "${WARNCFLAGS}"
   VERSION ${LT_VERSION} SOVERSION ${LT_SOVERSION}
@@ -49,6 +49,10 @@ target_include_directories(nghttp2 INTERFACE
     "${CMAKE_CURRENT_SOURCE_DIR}/includes"
     )

+if(NOT BUILD_SHARED_LIBS)
+  target_compile_definitions(nghttp2 PUBLIC "-DNGHTTP2_STATICLIB")
+endif()
+
 if(HAVE_CUNIT OR ENABLE_STATIC_LIB)
   # Static library (for unittests because of symbol visibility)
   add_library(nghttp2_static STATIC ${NGHTTP2_SOURCES})
EOF

if [[ "${OS}" == "Windows_NT" ]]; then
  git apply nghttp2_cmakelists.diff
  mkdir build
  cd build
  cmake -G "Ninja" -DCMAKE_INSTALL_PREFIX="$THIRDPARTY_BUILD" \
    -DENABLE_STATIC_LIB=on \
    -DENABLE_LIB_ONLY=on \
    ..
  ninja
  ninja install
  cp "lib/CMakeFiles/nghttp2.dir/nghttp2.pdb" "$THIRDPARTY_BUILD/lib/nghttp2.pdb"
else
  ./configure --prefix="$THIRDPARTY_BUILD" --enable-shared=no --enable-lib-only
  make V=1 install
fi
