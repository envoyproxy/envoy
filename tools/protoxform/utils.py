import os

from tools.type_whisperer.api_type_db_pb2 import TypeDb

from google.protobuf import text_format

typedb = None


def GetTypeDb():
  assert typedb != None
  return typedb


def LoadTypeDb(type_db_path):
  global typedb
  typedb = TypeDb()
  with open(type_db_path, 'r') as f:
    text_format.Merge(f.read(), typedb)
