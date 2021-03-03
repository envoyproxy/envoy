#!/usr/bin/env python3

# Generate RST lists of extensions grouped by their security posture.

from collections import defaultdict
import json
import os
import pathlib
import subprocess
import sys


def FormatItem(extension, metadata):
  if metadata['undocumented']:
    item = '* %s' % extension
  else:
    item = '* :ref:`%s <extension_%s>`' % (extension, extension)
  if metadata['status'] == 'alpha':
    item += ' (alpha)'
  return item


if __name__ == '__main__':
  extension_db_path = os.getenv("EXTENSION_DB_PATH")
  generated_rst_dir = os.getenv("GENERATED_RST_DIR")
  security_rst_root = os.path.join(
    generated_rst_dir, "intro/arch_overview/security")


  if not os.path.exists(extension_db_path):
      subprocess.run("tools/extensions/generate_extension_db".split())

  extension_db = json.loads(pathlib.Path(extension_db_path).read_text())
  security_postures = defaultdict(list)
  for extension, metadata in extension_db.items():
    security_postures[metadata['security_posture']].append(extension)

  for sp, extensions in security_postures.items():
    output_path = pathlib.Path(security_rst_root, 'secpos_%s.rst' % sp)
    content = '\n'.join(
        FormatItem(extension, extension_db[extension])
        for extension in sorted(extensions)
        if extension_db[extension]['status'] != 'wip')
    output_path.write_text(content)
