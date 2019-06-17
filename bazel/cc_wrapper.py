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


def modify_driver_args(input_driver_flags):
  # Detect if we're building for C++ or vanilla C.
  if any(map(is_cpp_flag, input_driver_flags)):
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
  if "-static-libstdc++" in input_driver_flags or "-stdlib=libc++" in envoy_cxxflags:
    for arg in input_driver_flags:
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
    argv += input_driver_flags

  # Bazel will add -fuse-ld=gold in some cases, gcc/clang will take the last -fuse-ld argument,
  # so whenever we see lld once, add it to the end.
  if "-fuse-ld=lld" in argv:
    argv.append("-fuse-ld=lld")

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

  return compiler, argv


def main():
  # Append CXXFLAGS to correctly detect include paths for either libstdc++ or libc++.
  if sys.argv[1:5] == ["-E", "-xc++", "-", "-v"]:
    os.execv(envoy_real_cxx, [envoy_real_cxx] + sys.argv[1:] + shlex.split(envoy_cxxflags))

  if sys.argv[1].startswith("@"):
    # Read flags from file
    flagfile_path = sys.argv[1][1:]
    with open(flagfile_path, "r") as fd:
      input_driver_flags = fd.read().splitlines()

    # Compute new args
    compiler, new_driver_args = modify_driver_args(input_driver_flags)

    # Write args to temp file
    (new_flagfile_fd, new_flagfile_path) = tempfile.mkstemp(dir="./", suffix=".linker-params")

    with closing_fd(new_flagfile_fd):
      for arg in new_driver_args:
        os.write(new_flagfile_fd, arg + "\n")

    # Provide new arguments using the temp file containing the args
    new_args = ["@" + new_flagfile_path]
  else:
    # TODO(https://github.com/bazelbuild/bazel/issues/7687): Remove this branch
    # when Bazel 0.27 is released.
    compiler, new_args = modify_driver_args(sys.argv[1:])

  os.execv(compiler, [compiler] + new_args)


if __name__ == "__main__":
  main()
