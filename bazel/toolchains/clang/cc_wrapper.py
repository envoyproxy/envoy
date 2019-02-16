#!/usr/bin/python
import os

compiler = "/usr/lib/llvm-8/bin/clang"
envoy_cflags = ""
envoy_cxxflags = ""

if __name__ == "__main__":
  real_wrapper = ""
  with open(os.path.join(os.path.dirname(__file__), "..", "..", "cc_wrapper.py")) as f:
    real_wrapper = f.read()
  real_wrapper = real_wrapper.replace("{ENVOY_REAL_CC}", "\"/usr/lib/llvm-8/bin/clang\"")
  real_wrapper = real_wrapper.replace("{ENVOY_REAL_CXX}", "\"/usr/lib/llvm-8/bin/clang++\"")
  real_wrapper = real_wrapper.replace("{ENVOY_CFLAGS}", "\"\"")
  real_wrapper = real_wrapper.replace("{ENVOY_CXXFLAGS}", "\"\"")
  exec(real_wrapper)
