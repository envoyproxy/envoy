import os

from tools.type_whisperer.api_type_db_pb2 import TypeDb

from google.protobuf import text_format

_typedb = None


def GetTypeDb():
  assert _typedb != None
  return _typedb


def LoadTypeDb(type_db_path):
  global _typedb
  _typedb = TypeDb()
  with open(type_db_path, 'r') as f:
    text_format.Merge(f.read(), _typedb)
