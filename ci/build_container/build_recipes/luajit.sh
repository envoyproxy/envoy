#!/bin/bash

set -e

VERSION=2.0.5

wget -O LuaJIT-"$VERSION".tar.gz https://github.com/LuaJIT/LuaJIT/archive/v"$VERSION".tar.gz
tar xf LuaJIT-"$VERSION".tar.gz
cd LuaJIT-"$VERSION"

# Fixup Makefile with things that cannot be set via env var.
cat > ../luajit_make.diff << 'EOF'
diff --git a/src/Makefile b/src/Makefile
index f7f81a4..0a8feff 100644
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
@@ -229,6 +229,7 @@ TARGET_ALDFLAGS= $(LDOPTIONS) $(TARGET_XLDFLAGS) $(TARGET_FLAGS) $(TARGET_LDFLAG
 TARGET_ASHLDFLAGS= $(LDOPTIONS) $(TARGET_XSHLDFLAGS) $(TARGET_FLAGS) $(TARGET_SHLDFLAGS)
 TARGET_ALIBS= $(TARGET_XLIBS) $(LIBS) $(TARGET_LIBS)
 
+$(warning $(TARGET_CC) $(TARGET_TCFLAGS) -E lj_arch.h -dM)
 TARGET_TESTARCH=$(shell $(TARGET_CC) $(TARGET_TCFLAGS) -E lj_arch.h -dM)
 ifneq (,$(findstring LJ_TARGET_X64 ,$(TARGET_TESTARCH)))
   TARGET_LJARCH= x64
@@ -251,7 +252,7 @@ ifneq (,$(findstring LJ_TARGET_MIPS ,$(TARGET_TESTARCH)))
   endif
   TARGET_LJARCH= mips
 else
-  $(error Unsupported target architecture)
+  $(error Unsupported target architecture $(TARGET_TESTARCH))
 endif
 endif
 endif
@@ -564,7 +565,7 @@ endif
 
 Q= @
 E= @echo
-#Q=
+Q=
 #E= @:
 
 ##############################################################################
EOF
patch -p1 < ../luajit_make.diff

DEFAULT_CC=${CC} TARGET_CFLAGS=${CFLAGS} TARGET_LDFLAGS=${CFLAGS} CFLAGS="" make V=1 PREFIX="$THIRDPARTY_BUILD" install
