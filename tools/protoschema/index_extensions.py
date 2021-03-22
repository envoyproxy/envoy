
import io
import json
import os
import re
import shutil
import subprocess
import sys


sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]


class ProtoSchemaExtensionsIndexer(object):

    def __init__(self):
        self.out_dir = os.path.abspath("out")

    def parse_schema(self, target):
        with io.open(target, encoding="utf-8") as f:
            return json.load(f)

    def get_extension_name(self, filename):
        parsed = self.parse_schema(filename)
        if "[#extension:" in parsed.get("description", []):
            pattern = r'\[\#extension\: (.+)\]'
            found = re.findall(pattern, parsed['description'])
            if found:
                return found[0]
            else:
                print(f"PROBLEM SCHEMA FILE {filename}")
                print(parsed["description"])

    def index(self):
        schemas = {}
        for root, dirs, files in os.walk(self.out_dir):
            for filename in files:
                if filename.endswith(".jsonschema"):
                    filepath = os.path.join(root, filename)
                    extension_name = self.get_extension_name(filepath)
                    if extension_name:
                        # print(f"ADD EXTENSION {extension_name}")
                        schemas[extension_name] = filepath

        with open(os.path.join(self.out_dir, "extension_schemas.json"), "w") as f:
            json.dump(schemas, f)


def main():
    subprocess.run(["./tools/protoschema/dump_jsonschema"])
    ProtoSchemaExtensionsIndexer().index()


if __name__ == '__main__':
    main()
