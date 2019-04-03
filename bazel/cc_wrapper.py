#!/usr/bin/python
import contextlib
import os
import shlex
import sys
import tempfile

envoy_real_cc = {ENVOY_REAL_CC}
envoy_real_cxx = {ENVOY_REAL_CXX}
envoy_cflags = {ENVOY_CFLAGS}
envoy_cxxflags = {ENVOY_CXXFLAGS}


@contextlib.contextmanager
def closing_fd(fd):
  try:
    yield fd
  finally:
    os.close(fd)


def sanitize_flagfile(in_path, out_fd):
  with open(in_path, "rb") as in_fp:
    for line in in_fp:
      if line != "-lstdc++\n":
        os.write(out_fd, line)
      elif "-stdlib=libc++" in envoy_cxxflags:
        os.write(out_fd, "-lc++\n")


# Is the arg a flag indicating that we're building for C++ (rather than C)?
def is_cpp_flag(arg):
  return arg in ["-static-libstdc++", "-stdlib=libc++", "-lstdc++", "-lc++"
                ] or arg.startswith("-std=c++") or arg.startswith("-std=gnu++")


def main():
  # Append CXXFLAGS to correctly detect include paths for either libstdc++ or libc++.
  if sys.argv[1:5] == ["-E", "-xc++", "-", "-v"]:
    os.execv(envoy_real_cxx, [envoy_real_cxx] + sys.argv[1:] + shlex.split(envoy_cxxflags))

  # Detect if we're building for C++ or vanilla C.
  if any(map(is_cpp_flag, sys.argv[1:])):
    compiler = envoy_real_cxx
    # Append CXXFLAGS to all C++ targets (this is mostly for dependencies).
    argv = shlex.split(envoy_cxxflags)
  else:
    compiler = envoy_real_cc
    # Append CFLAGS to all C targets (this is mostly for dependencies).
    argv = shlex.split(envoy_cflags)

  # Either:
  # a) remove all occurrences of -lstdc++ (when statically linking against libstdc++),
  # b) replace all occurrences of -lstdc++ with -lc++ (when linking against libc++).
  if "-static-libstdc++" in sys.argv[1:] or "-stdlib=libc++" in envoy_cxxflags:
    for arg in sys.argv[1:]:
      if arg == "-lstdc++":
        if "-stdlib=libc++" in envoy_cxxflags:
          argv.append("-lc++")
      elif arg.startswith("-Wl,@"):
        # tempfile.mkstemp will write to the out-of-sandbox tempdir
        # unless the user has explicitly set environment variables
        # before starting Bazel. But here in $PWD is the Bazel sandbox,
        # which will be deleted automatically after the compiler exits.
        (flagfile_fd, flagfile_path) = tempfile.mkstemp(dir="./", suffix=".linker-params")
        with closing_fd(flagfile_fd):
          sanitize_flagfile(arg[len("-Wl,@"):], flagfile_fd)
        argv.append("-Wl,@" + flagfile_path)
      else:
        argv.append(arg)
  else:
    argv += sys.argv[1:]

  # Add compiler-specific options
  if "clang" in compiler:
    # This ensures that STL symbols are included.
    # See https://github.com/envoyproxy/envoy/issues/1341
    argv.append("-fno-limit-debug-info")
    argv.append("-Wthread-safety")
    argv.append("-Wgnu-conditional-omitted-operand")
  elif "gcc" in compiler or "g++" in compiler:
    # -Wmaybe-initialized is warning about many uses of absl::optional. Disable
    # to prevent build breakage. This option does not exist in clang, so setting
    # it in clang builds causes a build error because of unknown command line
    # flag.
    # See https://github.com/envoyproxy/envoy/issues/2987
    argv.append("-Wno-maybe-uninitialized")

  os.execv(compiler, [compiler] + argv)


if __name__ == "__main__":
  main()
