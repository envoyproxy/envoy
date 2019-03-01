#!/usr/bin/env python

import sys

LUAJIT_VERSION = '2.1.0-beta3'
LUAJIT_FILE_URL = 'https://github.com/LuaJIT/LuaJIT/archive/v' + LUAJIT_VERSION + '.tar.gz'
LUAJIT_FILE_SHA256 = '409f7fe570d3c16558e594421c47bdd130238323c9d6fd6c83dedd2aaeb082a8'
LUAJIT_FILE_PREFIX = 'LuaJIT-' + LUAJIT_VERSION

# TODO(cmluciano): Bump to release 2.8
# This sha is specifically chosen to fix ppc64le builds that require inclusion
# of asm/ptrace.h
GPERFTOOLS_VERSION = 'fc00474ddc21fff618fc3f009b46590e241e425e'
GPERFTOOLS_FILE_URL = 'https://github.com/gperftools/gperftools/archive/' + GPERFTOOLS_VERSION + '.tar.gz'
GPERFTOOLS_FILE_SHA256 = '18574813a062eee487bc1b761e8024a346075a7cb93da19607af362dc09565ef'
GPERFTOOLS_FILE_PREFIX = 'gperftools-' + GPERFTOOLS_VERSION

RECIPES = dict(
    luajit=dict(
        version=LUAJIT_VERSION,
        url=LUAJIT_FILE_URL,
        sha256=LUAJIT_FILE_SHA256,
        strip_prefix=LUAJIT_FILE_PREFIX,
    ),
    gperftools=dict(
        version=GPERFTOOLS_VERSION,
        url=GPERFTOOLS_FILE_URL,
        sha256=GPERFTOOLS_FILE_SHA256,
        strip_prefix=GPERFTOOLS_FILE_PREFIX,
    ))

if __name__ == '__main__':
  if len(sys.argv) != 2:
    print('Usage: %s <recipe name>' % sys.argv[0])
    sys.exit(1)
  name = sys.argv[1]
  if name not in RECIPES:
    print('Unknown recipie: %s' % recipe)
    sys.exit(1)
  recipe = RECIPES[name]
  print("""
  export VERSION={}
  export FILE_URL={}
  export FILE_SHA256={}
  export FILE_PREFIX={}
  """.format(
      recipe['version'],
      recipe['url'],
      recipe['sha256'],
      recipe['strip_prefix'],
  ))
