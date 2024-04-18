import importlib

from tools.type_whisperer.api_type_db_pb2 import TypeDb

from google.protobuf import text_format

PROTO_FILES = (
    "google.api.annotations", "validate.validate", "envoy.annotations.deprecation",
    "envoy.annotations.resource", "udpa.annotations.migrate", "udpa.annotations.security",
    "udpa.annotations.status", "udpa.annotations.sensitive", "udpa.annotations.versioning",
    "xds.annotations.v3.status")

_typedb = None


def get_type_db():
    assert _typedb != None
    return _typedb


def load_type_db(type_db_path):
    global _typedb
    _typedb = TypeDb()
    with open(type_db_path, 'r') as f:
        text_format.Merge(f.read(), _typedb)


def load_protos():
    for package in PROTO_FILES:
        importlib.import_module(f"{package}_pb2")
