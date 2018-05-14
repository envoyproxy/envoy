"""Convert from transport socket capture to fuzz test case.

Converts from envoy.extensions.common.tap.v2alpha.Trace proto to
test.integration.CaptureFuzzTestCase.

Usage: capture_fuzz_gen.py <listener capture> [<cluster capture>]
"""

import functools
import sys

from google.protobuf import empty_pb2
from google.protobuf import text_format

from envoy.extensions.common.tap.v2alpha import capture_pb2
from test.integration import capture_fuzz_pb2

# Collapse adjacent event in the trace that are of the same type.
def Coalesce(trace):
  if not trace.events:
    return []
  events = [trace.events[0]]
  for event in trace.events[1:]:
    if events[-1].HasField('read') and event.HasField('read'):
      events[-1].read.data += event.read.data
    elif events[-1].HasField('write') and event.HasField('write'):
      events[-1].write.data += event.write.data
    else:
      events.append(event)
  return events

# Convert from transport socket Event to test Event.
def ToTestEvent(direction, event):
  test_event = capture_fuzz_pb2.Event()
  if event.HasField('read'):
    setattr(test_event, '%s_send_bytes' % direction, event.read.data)
  elif event.HasField('write'):
    getattr(test_event, '%s_recv_bytes' % direction).MergeFrom(empty_pb2.Empty())
  return test_event

def ToDownstreamTestEvent(event):
  return ToTestEvent('downstream', event)

def ToUpstreamTestEvent(event):
  return ToTestEvent('upstream', event)

# Zip together the listener/cluster events to produce a single trace for replay.
def TestCaseGen(listener_events, cluster_events):
  test_case = capture_fuzz_pb2.CaptureFuzzTestCase()
  if not listener_events:
    return test_case
  test_case.events.extend([ToDownstreamTestEvent(listener_events[0])])
  del listener_events[0]
  while listener_events or cluster_events:
    if not listener_events:
      test_case.events.extend(map(ToUpstreamTestEvent, cluster_events))
      return test_case
    if not cluster_events:
      test_case.events.extend(map(ToDownstreamTestEvent, listener_events))
      return test_case
    if listener_events[0].timestamp.ToDatetime() < cluster_events[0].timestamp.ToDatetime():
      test_case.events.extend([ToDownstreamTestEvent(listener_events[0])])
      del listener_events[0]
    test_case.events.extend([ToUpstreamTestEvent(cluster_events[0])])
    del cluster_events[0]

def CaptureFuzzGen(listener_path, cluster_path=None):
  listener_trace = capture_pb2.Trace()
  with open(listener_path, 'r') as f:
    text_format.Merge(f.read(), listener_trace)
  listener_events = Coalesce(listener_trace)

  cluster_trace = capture_pb2.Trace()
  if cluster_path:
    with open(cluster_path, 'r') as f:
      text_format.Merge(f.read(), cluster_trace)
    cluster_events = Coalesce(cluster_trace)

  print TestCaseGen(listener_events, cluster_events)


if __name__ == '__main__':
  if len(sys.argv) < 2 or len(sys.argv) > 3:
    print 'Usage: %s <listener capture> [<cluster capture>]' % sys. argv[0]
    sys.exit(1)
  CaptureFuzzGen(*sys.argv[1:])
