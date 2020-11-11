#!/usr/bin/python

import os
import sys
import re
import time
import tempfile
import signal
import subprocess
import unittest
from rules_python.python.runfiles import runfiles

class OutOfProcEnvoyTests(unittest.TestCase):
  """
  We test that Envoy process behaves correctly.
  """

  @classmethod
  def setUp(self):
    r = runfiles.Create()
    envoy_path = None
    if sys.platform == 'win32':
      envoy_path = r.Rlocation("envoy/source/exe/envoy-static.exe")
    else:
      envoy_path = r.Rlocation("envoy/source/exe/envoy-static")
    if not os.path.isfile(envoy_path):
      raise Exception("Could not find Envoy")

    envoy_config = r.Rlocation('envoy/test/config/integration/google_com_proxy_port_0.yaml')
    with open(envoy_config, 'r') as f:
      content = f.read()

    content = content.replace("{{ ip_any_address }}", "127.0.0.1")
    content = content.replace("{{ dns_lookup_family }}", "V4_ONLY")
    if os.name != 'nt':
      content = content.replace("{{ null_device_path }}", "/dev/null")
    else:
      content = content.replace("{{ null_device_path }}", "NUL")

    envoy_config_new = os.path.join(os.getenv('TEST_TMPDIR'), 'google_com_proxy_port_0.yaml')
    with open(envoy_config_new, "w") as f:
      f.write(content)

    try:  # Windows-specific.
      creationflags = subprocess.CREATE_NEW_PROCESS_GROUP
    except AttributeError:
      creationflags = 0
    self.envoy_proc = subprocess.Popen([envoy_path, "--config-path", envoy_config_new],
                                       stdin=subprocess.PIPE,
                                       creationflags=creationflags)
    time.sleep(2)

  @classmethod
  def tearDown(self):
    self.envoy_proc.kill()

  def test_envoy_closes_on_ctrl_break_event(self):
    self.assertTrue(self.envoy_proc.poll() == None)
    ctrl_break_signal = None
    if sys.platform == 'win32':
      # On Windows sending a ctrl + c signal is quite tricky
      # so we test that Envoy stops on ctrl + break.
      ctrl_break_signal = signal.CTRL_BREAK_EVENT
    else:
      ctrl_break_signal = signal.SIGTERM
    self.envoy_proc.send_signal(ctrl_break_signal)
    time.sleep(2)
    poll = self.envoy_proc.poll()
    self.assertTrue(poll != None)


if __name__ == '__main__':
  unittest.main()