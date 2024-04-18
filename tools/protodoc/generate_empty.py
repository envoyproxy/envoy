# Generate pseudo API docs for extensions that have google.protobuf.Empty
# config.

import json
import os
import pathlib
import sys
import tarfile

from tools.protodoc import protodoc as _protodoc
from tools.protodoc.jinja import env as jinja_env


def generate_empty_extension_docs(protodoc, extension, details, api_extensions_root):
    extension_root = pathlib.Path(details['path'])
    path = pathlib.Path(api_extensions_root, extension_root, 'empty', extension_root.name + '.rst')
    path.parent.mkdir(parents=True, exist_ok=True)
    description = details.get('description', '')
    reflink = ''
    if 'ref' in details:
        reflink = '%s %s.' % (
            details['title'],
            _protodoc.format_internal_link('configuration overview', details['ref']))
    content = jinja_env.get_template("empty.rst.tpl").render(
        header=details['title'],
        description=description,
        reflink=reflink,
        extension=protodoc._extension(extension))
    path.write_text(content)


def main():
    empty_extensions_path = sys.argv[1]
    output_filename = sys.argv[2]
    generated_rst_dir = os.path.dirname(output_filename)
    api_extensions_root = os.path.join(generated_rst_dir, "api-v3/config")

    empty_extensions = json.loads(pathlib.Path(empty_extensions_path).read_text())
    protodoc = _protodoc.RstFormatVisitor()
    for extension, details in empty_extensions.items():
        generate_empty_extension_docs(protodoc, extension, details, api_extensions_root)

    with tarfile.open(output_filename, "w:gz") as tar:
        tar.add(generated_rst_dir, arcname=".")


if __name__ == '__main__':
    main()
