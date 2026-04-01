import sys
import inspect

from python.protoc_gen_validate.validator import validate, validate_all, ValidationFailed

from tests.harness.harness_pb2 import TestCase, TestResult
from tests.harness.cases.bool_pb2 import *
from tests.harness.cases.bytes_pb2 import *
from tests.harness.cases.enums_pb2 import *
from tests.harness.cases.enums_pb2 import *
from tests.harness.cases.messages_pb2 import *
from tests.harness.cases.numbers_pb2 import *
from tests.harness.cases.oneofs_pb2 import *
from tests.harness.cases.repeated_pb2 import *
from tests.harness.cases.strings_pb2 import *
from tests.harness.cases.maps_pb2 import *
from tests.harness.cases.wkt_any_pb2 import *
from tests.harness.cases.wkt_duration_pb2 import *
from tests.harness.cases.wkt_nested_pb2 import *
from tests.harness.cases.wkt_wrappers_pb2 import *
from tests.harness.cases.wkt_timestamp_pb2 import *
from tests.harness.cases.kitchen_sink_pb2 import *

message_classes = {}
for k, v in inspect.getmembers(sys.modules[__name__], inspect.isclass):
    if 'DESCRIPTOR' in dir(v):
        message_classes[v.DESCRIPTOR.full_name] = v


if __name__ == "__main__":
    read = sys.stdin.buffer.read()

    testcase = TestCase()
    testcase.ParseFromString(read)

    test_class = message_classes[testcase.message.TypeName()]
    test_msg = test_class()
    testcase.message.Unpack(test_msg)

    try:
        result = TestResult()
        valid = validate(test_msg)
        result.Valid = True
    except ValidationFailed as e:
        result.Valid = False
        result.Reasons[:] = [repr(e)]

    try:
        result_all = TestResult()
        valid = validate_all(test_msg)
        result_all.Valid = True
    except ValidationFailed as e:
        result_all.Valid = False
        result_all.Reasons[:] = [repr(e)]

    if result.Valid != result_all.Valid:
        raise ValueError(f"validation results mismatch, validate: {result.Valid}, "
                         f"validate_all: {result_all.Valid}")
    if not result.Valid:
        reason = list(result.Reasons)[0]  # ValidationFailed("reason")
        reason = reason[18:-2]  # reason
        reason_all = list(result_all.Reasons)[0]  # ValidationFailed("reason1\nreason2\n...reason")
        reason_all = reason_all[18:-2]  # reason1\nreason2\n...reason
        if not reason_all.startswith(reason):
            raise ValueError(f"different first message, validate: {reason}, "
                             f"validate_all: {reason_all}")

    sys.stdout = open(sys.stdout.fileno(), mode='w', encoding='utf8')
    sys.stdout.write(result.SerializeToString().decode("utf-8"))
