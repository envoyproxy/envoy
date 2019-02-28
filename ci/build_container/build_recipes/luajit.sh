#!/bin/bash

set -e

if [[ "${OS}" == "Windows_NT" ]]; then
  exit 0
fi

SCRIPT_DIR="$(dirname "${BASH_SOURCE[0]}")"

$($SCRIPT_DIR/versions.py luajit)

FILE_NAME=$(basename "$FILE_URL")

curl "$FILE_URL" -sLo "$FILE_NAME" \
  && echo "$FILE_SHA256" "$FILE_NAME" | sha256sum --check
tar xf "$FILE_NAME"

cd "$FILE_PREFIX"

# Fixup Makefile with things that cannot be set via env var.
cat > luajit_make.diff << 'EOF'
diff --git a/src/Makefile b/src/Makefile
index f56465d..3f4f2fa 100644
--- a/src/Makefile
+++ b/src/Makefile
@@ -27,7 +27,7 @@ NODOTABIVER= 51
 DEFAULT_CC = gcc
 #
 # LuaJIT builds as a native 32 or 64 bit binary by default.
-CC= $(DEFAULT_CC)
+CC ?= $(DEFAULT_CC)
 #
 # Use this if you want to force a 32 bit build on a 64 bit multilib OS.
 #CC= $(DEFAULT_CC) -m32
@@ -71,10 +71,10 @@ CCWARN= -Wall
 # as dynamic mode.
 #
 # Mixed mode creates a static + dynamic library and a statically linked luajit.
-BUILDMODE= mixed
+#BUILDMODE= mixed
 #
 # Static mode creates a static library and a statically linked luajit.
-#BUILDMODE= static
+BUILDMODE= static
 #
 # Dynamic mode creates a dynamic library and a dynamically linked luajit.
 # Note: this executable will only run when the library is installed!
@@ -99,7 +99,7 @@ XCFLAGS=
 # enabled by default. Some other features that *might* break some existing
 # code (e.g. __pairs or os.execute() return values) can be enabled here.
 # Note: this does not provide full compatibility with Lua 5.2 at this time.
-#XCFLAGS+= -DLUAJIT_ENABLE_LUA52COMPAT
+XCFLAGS+= -DLUAJIT_ENABLE_LUA52COMPAT
 #
 # Disable the JIT compiler, i.e. turn LuaJIT into a pure interpreter.
 #XCFLAGS+= -DLUAJIT_DISABLE_JIT
@@ -587,7 +587,7 @@ endif

 Q= @
 E= @echo
-#Q=
+Q=
 #E= @:

 ##############################################################################
EOF

patch -p1 < luajit_make.diff

# Default MACOSX_DEPLOYMENT_TARGET is 10.4, which will fail the build at link time on macOS 10.14:
# ld: library not found for -lgcc_s.10.4
# This doesn't affect other platforms
MACOSX_DEPLOYMENT_TARGET=10.6 DEFAULT_CC=${CC} TARGET_CFLAGS=${CFLAGS} TARGET_LDFLAGS=${CFLAGS} \
  CFLAGS="" make V=1 PREFIX="$THIRDPARTY_BUILD" install
