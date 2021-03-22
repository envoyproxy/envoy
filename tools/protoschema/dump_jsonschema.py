
import io
import json
import os
import re
import shutil
import subprocess
import sys


sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]


class ProtoSchemaDumper(object):
    _out_dir = "out"
    _out_file = "envoy.jsonschema"
    _jsonschema_out = (
        "disallow_additional_properties",
        "prefix_schema_files_with_package")
    _jsonschema_bin_dir = (
        "com_github_chrusty_protoc_gen_jsonschema/protoc-gen-jsonschema_")
    _proto_deps = (
        "com_github_cncf_udpa",
        "com_envoyproxy_protoc_gen_validate",
        "com_google_googleapis",
        "opencensus_proto",
        "prometheus_metrics_model")
    _protobuf_well_known = "com_google_protobuf/_virtual_imports"
    _protoc_bin = "com_google_protobuf/protoc"

    def __init__(self):
        self.setpath()

    @property
    def protoc(self):
        return self.external_path(self._protoc_bin)

    @property
    def includes(self):
        _includes = ["-I", self.proto_dir]
        for inc in self._proto_deps:
            _includes += ["-I", self.external_path(inc)]
        for path in os.listdir(self.external_path(self._protobuf_well_known)):
            _includes += [
                "-I",
                self.external_path(
                    f"{self._protobuf_well_known}/{path}")]
        return _includes

    @property
    def proto_dir(self):
        return self.external_path("envoy_api_canonical")

    @property
    def jsonschema(self):
        return (
            f"--jsonschema_out={','.join(self._jsonschema_out)}:"
            f"{self._out_dir}")

    def protoc_command(self, target):
        return [self.protoc] + self.includes + [
            self.jsonschema,
            f"--proto_path={self.proto_dir}",
            target]

    def external_path(self, path):
        return os.path.abspath(os.path.join("external", path))

    def setpath(self):
        jsonschema_dir = self.external_path(self._jsonschema_bin_dir)
        os.environ["PATH"] = f"{os.environ['PATH']}:{jsonschema_dir}"

    def run_protoc(self, target):
        # print(f"PROTOC: {' '.join(self.protoc_command(target))}")
        subprocess.run(self.protoc_command(target))

    def dump(self):
        if not os.path.exists(self._out_dir):
            os.mkdir(self._out_dir)
        for root, dirs, files in os.walk(self.proto_dir):
            if not os.path.basename(root).startswith("v3"):
                # this is not quite correct 8(
                print(f"IGNORE PROTOS: {root}")
                continue
            for filename in files:
                if filename.endswith(".proto"):
                    self.run_protoc(os.path.join(root, filename))


def main():
    ProtoSchemaDumper().dump()
    shutil.copytree("out", "/tmp/tmpout")


if __name__ == '__main__':
    main()
