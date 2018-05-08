"""Tests for capture2pcap."""

import os
import subprocess as sp
import sys

import capture2pcap

# Validate that the captured trace when run through capture2cap | tshark matches
# a golden output file for the tshark dump. Since we run capture2pcap in a
# subshell with a limited environment, the inferred time zone should be UTC.
if __name__ == '__main__':
  srcdir = os.path.join(os.getenv('TEST_SRCDIR'), 'envoy_api')
  capture_path = os.path.join(srcdir, 'tools/data/capture2pcap_h2_ipv4.pb_text')
  expected_path = os.path.join(srcdir, 'tools/data/capture2pcap_h2_ipv4.txt')
  pcap_path = os.path.join(os.getenv('TEST_TMPDIR'), 'generated.pcap')

  capture2pcap.Capture2Pcap(capture_path, pcap_path)
  actual_output = sp.check_output(
      ['tshark', '-r', pcap_path, '-d', 'tcp.port==10000,http2', '-P'])
  with open(expected_path, 'r') as f:
    expected_output = f.read()
  if actual_output != expected_output:
    print 'Mismatch'
    print 'Expected: %s' % expected_output
    print 'Actual: %s' % actual_output
    sys.exit(1)
