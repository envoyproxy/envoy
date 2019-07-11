#!/usr/bin/env python3

import sys
import os.path

while True:
  line = sys.stdin.readline()
  if not line:
    break
  if line.startswith("SF:"):
    filename = line[3:-1]
    line = "SF:" + os.path.relpath(os.path.realpath(filename)) + "\n"
  sys.stdout.write(line)
