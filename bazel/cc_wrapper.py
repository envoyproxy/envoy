#!/usr/bin/python
import contextlib
import os
import sys
import tempfile

envoy_real_cc = {ENVOY_REAL_CC}
envoy_real_cxx = {ENVOY_REAL_CXX}


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


def main():
  compiler = envoy_real_cc

  # Debian's packaging of Clang requires `-no-canonical-prefixes` to print
  # consistent include paths, but Bazel 0.10 only sets that option at compile
  # time. We inject it here for the configuration of `@local_config_cc//`.
  #
  # https://github.com/bazelbuild/bazel/issues/3977
  # https://github.com/bazelbuild/bazel/issues/4572
  # https://bazel-review.googlesource.com/c/bazel/+/39951
  if sys.argv[1:] == ["-E", "-xc++", "-", "-v"] and "clang" in compiler:
    os.execv(envoy_real_cxx,
             [envoy_real_cxx, "-E", "-", "-v", "-no-canonical-prefixes"])

  # `g++` and `gcc -lstdc++` have similar behavior and Bazel treats them as
  # interchangeable, but `gcc` will ignore the `-static-libstdc++` flag.
  # This check lets Envoy statically link against libstdc++ to be more
  # portable between installed glibc versions.
  #
  # Similar behavior exists for Clang's `-stdlib=libc++` flag, so we handle
  # it in the same test.
  if "-static-libstdc++" in sys.argv[1:] or "-stdlib=libc++" in sys.argv[1:]:
    compiler = envoy_real_cxx
    argv = []
    for arg in sys.argv[1:]:
      if arg == "-lstdc++":
        pass
      elif arg.startswith("-Wl,@"):
        # tempfile.mkstemp will write to the out-of-sandbox tempdir
        # unless the user has explicitly set environment variables
        # before starting Bazel. But here in $PWD is the Bazel sandbox,
        # which will be deleted automatically after the compiler exits.
        (flagfile_fd, flagfile_path) = tempfile.mkstemp(
            dir='./', suffix=".linker-params")
        with closing_fd(flagfile_fd):
          sanitize_flagfile(arg[len("-Wl,@"):], flagfile_fd)
        argv.append("-Wl,@" + flagfile_path)
      else:
        argv.append(arg)
  else:
    argv = sys.argv[1:]

  # Add compiler-specific options
  if "clang" in compiler:
    # This ensures that STL symbols are included.
    # See https://github.com/envoyproxy/envoy/issues/1341
    argv.append("-fno-limit-debug-info")
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
