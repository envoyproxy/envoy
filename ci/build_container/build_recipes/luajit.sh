#!/bin/bash

set -e

VERSION=2.0.5

wget -O LuaJIT-"$VERSION".tar.gz https://github.com/LuaJIT/LuaJIT/archive/v"$VERSION".tar.gz
tar xf LuaJIT-"$VERSION".tar.gz
cd LuaJIT-"$VERSION"

# Fixup Makefile with things that cannot be set via env var.
cat > ../luajit_make.diff << EOF
diff --git a/src/Makefile b/src/Makefile
index f7f81a4..e80c7ff 100644
--- a/src/Makefile
+++ b/src/Makefile
@@ -74,10 +74,10 @@ CCWARN= -Wall
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
@@ -102,7 +102,7 @@ XCFLAGS=
 # enabled by default. Some other features that *might* break some existing
 # code (e.g. __pairs or os.execute() return values) can be enabled here.
 # Note: this does not provide full compatibility with Lua 5.2 at this time.
-#XCFLAGS+= -DLUAJIT_ENABLE_LUA52COMPAT
+XCFLAGS+= -DLUAJIT_ENABLE_LUA52COMPAT
 #
 # Disable the JIT compiler, i.e. turn LuaJIT into a pure interpreter.
 #XCFLAGS+= -DLUAJIT_DISABLE_JIT
@@ -564,7 +564,7 @@ endif

 Q= @
 E= @echo
-#Q=
+Q=
 #E= @:

 ##############################################################################
EOF
patch -p1 < ../luajit_make.diff

make PREFIX="$THIRDPARTY_BUILD" install
