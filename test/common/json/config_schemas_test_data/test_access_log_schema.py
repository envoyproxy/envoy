from util import get_blob
from util import true, false

ACCESS_LOG_BLOB = {
    "access_log": [{
        "filter": {
            "type":
                "logical_and",
            "filters": [{
                "type": "not_healthcheck"
            }, {
                "type": "runtime",
                "key": "access_log.front_access_log"
            }]
        },
        "path": "/var/log/envoy/access.log"
    }, {
        "filter": {
            "type":
                "logical_or",
            "filters": [{
                "runtime_key": "access_log.access_error.status",
                "type": "status_code",
                "value": 500,
                "op": ">="
            }, {
                "type": "status_code",
                "value": 429,
                "op": "="
            }, {
                "runtime_key": "access_log.access_error.duration",
                "type": "duration",
                "value": 1000,
                "op": ">="
            }, {
                "type": "traceable_request"
            }]
        },
        "path": "/var/log/envoy/access_error.log"
    }]
}


def test(writer):
  for idx, item in enumerate(ACCESS_LOG_BLOB["access_log"]):
    writer.write_test_file(
        'Valid_idx_' + str(idx),
        schema='ACCESS_LOG_SCHEMA',
        data=get_blob(item),
        throws=False,
    )

  blob = get_blob(ACCESS_LOG_BLOB)['access_log'][1]
  blob['filter']['filters'][0]['op'] = '<'
  writer.write_test_file(
      'FilterOperatorIsNotSupportedLessThan',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = get_blob(ACCESS_LOG_BLOB)['access_log'][1]
  blob['filter']['filters'][0]['op'] = '<='
  writer.write_test_file(
      'FilterOperatorIsNotSupportedLessThanEqual',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = get_blob(ACCESS_LOG_BLOB)['access_log'][1]
  blob['filter']['filters'][0]['op'] = '>'
  writer.write_test_file(
      'FilterOperatorIsNotSupportedGreaterThan',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = {"path": "/dev/null", "filter": {"type": "unknown"}}
  writer.write_test_file(
      'FilterTypeIsNotSupported',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = {"path": "/dev/null", "filter": {"type": "logical_or", "filters": []}}
  writer.write_test_file(
      'LessThanTwoFiltersInListNoneLogicalOrThrows',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = {"path": "/dev/null", "filter": {"type": "logical_and", "filters": []}}
  writer.write_test_file(
      'LessThanTwoFiltersInListNoneLogicalAndThrows',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = {
      "path": "/dev/null",
      "filter": {
          "type": "logical_or",
          "filters": [{
              "type": "not_healthcheck"
          }]
      }
  }
  writer.write_test_file(
      'LessThanTwoFiltersInListOneLogicalOrThrows',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )

  blob = {
      "path": "/dev/null",
      "filter": {
          "type": "logical_and",
          "filters": [{
              "type": "not_healthcheck"
          }]
      }
  }
  writer.write_test_file(
      'LessThanTwoFiltersInListOneLogicalAndThrows',
      schema='ACCESS_LOG_SCHEMA',
      data=blob,
      throws=True,
  )
