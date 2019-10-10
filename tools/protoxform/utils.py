import os

from tools.type_whisperer.api_type_db_pb2 import TypeDb

from google.protobuf import text_format


def LoadTypeDb():
  typedb = TypeDb()
  with open(os.getenv('TYPE_DB_PATH'), 'r') as f:
    text_format.Merge(f.read(), typedb)
  return typedb
