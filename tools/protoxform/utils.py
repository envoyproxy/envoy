import os

from tools.type_whisperer.api_type_db_pb2 import TypeDb


def LoadTypeDb():
  typedb = TypeDb()
  with open(os.getenv('TYPE_DB_PATH'), 'rb') as f:
    typedb.MergeFromString(f.read())
  return typedb
