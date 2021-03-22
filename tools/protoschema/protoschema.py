
import json
import os
import re
import shutil
import subprocess
import sys


sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]


class ExtensionRequired(Exception):
    pass


class ProtoSchemaMangler(object):
    _bootstrap_schema = "envoy.config.bootstrap.v3/Bootstrap.jsonschema"

    def __init__(self, out_dir, out_file):
        self.out_dir = out_dir
        self.out_file = out_file
        self.extra_definitions = set()
        self.parse_extension_db()
        self.index_extension_schemas()
        self.out = {}
        self.definitions = {}

    @property
    def bootstrap_schema(self):
        return self.parse_schema(
            os.path.join(
                self.out_dir,
                self._bootstrap_schema))

    @property
    def extension_db_path(self):
        return os.environ["EXTENSION_DB_PATH"]

    def canonical_proto_name(self, extension):
        extension_name = self.extension_schemas[
            extension][4:].replace('/', '.')[:-11]
        return f"type.googleapis.com/{extension_name}"

    def handle_description(self, text):
        # remove next-free field
        # do something with the proto titles
        if '[#extension-category' in text:
            raise ExtensionRequired
        if '[#not-implemented-hide:]' in text:
            raise NotImplementedError
        return self.strip_annotations(text)

    def handle_extensions(self):
        extra_definitions = self.extra_definitions
        self.extra_definitions = set()
        definitions = {}
        for extension in extra_definitions:
            schema_file = self.extension_schemas.get(extension)
            if not schema_file:
                print(f"MISSING SCHEMA {extension}")
                continue
            extension_schema = self.parse_schema(schema_file)
            definitions = extension_schema.pop("definitions", {})

            for definition in definitions:
                if definition not in self.out["definitions"]:
                    self.out["definitions"][definition] = {}
                    try:
                        self.recurse_keys(
                            definitions[definition],
                            self.out["definitions"][definition])
                    except NotImplementedError:
                        pass
            extension_name = self.canonical_proto_name(
                extension).replace("/", "__")
            self.out["definitions"][extension_name] = {}
            self.recurse_keys(
                extension_schema,
                self.out["definitions"][extension_name])
        if self.extra_definitions:
            self.handle_extensions()

    def handle_extension_category(self, v, out):
        category = dict(
            self.parse_annotations(
                v['description'])).get('extension-category')
        extensions = self.extensions.get(category)

        schema = dict(
            type="object",
            allOf=[
                dict(properties={"xtype": dict(type="string", enum=[])}),
                dict(oneOf=[])])

        for extension in extensions:
            extension_schema = self.extension_schemas.get(extension)
            if not extension_schema:
                continue
            extension_name = self.canonical_proto_name(extension)
            schema["allOf"][0]["properties"]["xtype"]["enum"].append(extension_name)
            schema["allOf"][1]["oneOf"].append(
                {"if": dict(properties={"xtype": dict(const=extension_name)}),
                 "then": {"$ref": f'#/definitions/{extension_name.replace("/", "__")}'}})
            self.extra_definitions.add(extension)

        # if schema didnt get set revert to Any ?

        out.clear()
        out.update(schema)

    def handle_ref(self, v):
        return f'#/definitions/{v}'

    def index_extension_schemas(self):
        schemas = {}
        for root, dirs, files in os.walk(self.out_dir):
            for filename in files:
                if filename.endswith(".jsonschema"):
                    filepath = os.path.join(root, filename)
                    extension_name = self.parse_extension_name(filepath)
                    if extension_name:
                        schemas[extension_name] = filepath
        self.extension_schemas = schemas

    def mangle(self):
        self.recurse_keys(self.bootstrap_schema, self.out)
        self.handle_extensions()
        # sort definitions by key
        self.out["definitions"] = {
            k: self.out["definitions"][k]
            for k
            in sorted(self.out["definitions"].keys())}
        self.out["$schema"] = "http://json-schema.org/draft-04/schema#"
        with open(self.out_file, 'w') as f:
            f.write(json.dumps(self.out, indent=2))

    def parse_annotations(self, text):
        return re.findall(r"\[\#(.+)\: (.+)\]", text)

    def parse_extension_name(self, filename):
        parsed = self.parse_schema(filename)
        if "[#extension:" in parsed.get("description", []):
            pattern = r'\[\#extension\: (.+)\]'
            found = re.findall(pattern, parsed['description'])
            if found:
                return found[0]

    def parse_extension_db(self):
        self.extensions = {}
        with open(self.extension_db_path) as f:
            extension_db = json.load(f)
        for _k, _v in extension_db.items():
            for _cat in _v['categories']:
                self.extensions.setdefault(_cat, []).append(_k)

    def parse_schema(self, target):
        with open(target) as f:
            return json.load(f)

    def recurse_keys(self, schema, out):
        for k, v in schema.items():
            if k == '$ref':
                v = self.handle_ref(v)
            elif k == 'description' and isinstance(v, str):
                # description can be the name of a property
                # so filter that its a str to ensure its the
                # description *of* a property
                v = self.handle_description(v)
            # if k == "$schema":
                # hmmm
                # pass
            if isinstance(v, dict):
                out[k] = {}
                try:
                    self.recurse_keys(v, out[k])
                except NotImplementedError:
                    del out[k]
                except ExtensionRequired:
                    self.handle_extension_category(v, out[k])
            else:
                out[k] = v

    def strip_annotations(self, text):
        for k, v in self.parse_annotations(text):
            text = text.replace(f"[#{k}: {v}]", "")
        return text


class ProtoSchema(object):
    _out_dir = "out"
    _out_file = "envoy.schema.json"

    def mangle_jsonschema(self):
        ProtoSchemaMangler(
            self._out_dir,
            self._out_file).mangle()

    def save_jsonschema(self):
        # workaround
        target = os.path.join('/tmp/schema', self._out_file)
        if not os.path.exists("/tmp/schema"):
            os.mkdir('/tmp/schema')
        shutil.copyfile(
            self._out_file,
            target)

    def generate(self):
        self.mangle_jsonschema()
        self.save_jsonschema()


def main():
    subprocess.run([
        "./tools/extensions/generate_extension_db"])
    subprocess.run(["./tools/protoschema/index_extensions"])
    ProtoSchema().generate()


if __name__ == '__main__':
    main()
