#!/usr/bin/env python3

# Generate RST lists of extensions grouped by their security posture.

from collections import defaultdict
import os
import pathlib
import sys
import tarfile

from tools.base import utils


def format_item(extension, metadata):
    if metadata.get('undocumented'):
        item = '* %s' % extension
    else:
        item = '* :ref:`%s <extension_%s>`' % (extension, extension)
    if metadata.get('status') == 'alpha':
        item += ' (alpha)'
    return item


def main():
    metadata_filepath = sys.argv[1]
    output_filename = sys.argv[2]
    generated_rst_dir = os.path.dirname(output_filename)
    security_rst_root = os.path.join(generated_rst_dir, "intro/arch_overview/security")
    extension_db = utils.from_yaml(metadata_filepath)

    pathlib.Path(security_rst_root).mkdir(parents=True, exist_ok=True)

    security_postures = defaultdict(list)
    for extension, metadata in extension_db.items():
        security_postures[metadata['security_posture']].append(extension)

    for sp, extensions in security_postures.items():
        output_path = pathlib.Path(security_rst_root, 'secpos_%s.rst' % sp)
        content = '\n'.join(
            format_item(extension, extension_db[extension])
            for extension in sorted(extensions)
            if extension_db[extension].get('status') != 'wip')
        output_path.write_text(content)

    with tarfile.open(output_filename, "w") as tar:
        tar.add(generated_rst_dir, arcname=".")


if __name__ == '__main__':
    sys.exit(main())
