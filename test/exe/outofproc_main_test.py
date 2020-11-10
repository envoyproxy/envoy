
#!/usr/bin/python

import os
import sys
import re
import time
import tempfile
import signal
import subprocess
import unittest

class OutOfProcEnvoyTests(unittest.TestCase):
  """
  We test that Envoy process behaves correctly.
  """

  @staticmethod
  def find_envoy():
    """
    This method locates envoy binary.
    It's present at ./source/exe/envoy-static (at least for mac/bazel-asan/bazel-tsan),
    or at ./external/envoy/source/exe/envoy-static (for bazel-compile_time_options).
    """
    candidate = os.path.join('.', 'source', 'exe', 'envoy-static')
    if os.path.isfile(candidate):
      return candidate
    candidate = os.path.join('.', 'external', 'envoy', 'source', 'exe', 'envoy-static')
    if os.path.isfile(candidate):
      return candidate
    raise Exception("Could not find Envoy")

  @classmethod
  def setUp(self):
    envoy_path = OutOfProcEnvoyTests.find_envoy()
    envoy_config = os.path.join('.', 'test/config/integration/google_com_proxy_port_0.yaml')
    with open(envoy_config, 'r') as f:
      content = f.read()


    content = re.sub("{{ upstream_\d }}", "0", content)
    content = content.replace("{{ ip_any_address }}", "127.0.0.1")
    content = content.replace("{{ dns_lookup_family }}", "V4_ONLY")
    if os.name == 'nt':
      content = content.replace("{{ null_device_path }}", "/dev/null")
    else:
      content = content.replace("{{ null_device_path }}", "NUL")

    print(content)
    envoy_config_new = os.path.join(os.getenv('TEST_TMPDIR'), 'google_com_proxy_port_0.yaml')
    with open(envoy_config_new, "w") as f:
      f.write(content)

    try:  # Windows-specific.
      creationflags = subprocess.CREATE_NEW_PROCESS_GROUP
    except AttributeError:
      creationflags = 0
    self.envoy_proc = subprocess.Popen([envoy_path, "--config-path", envoy_config_new], stdin=subprocess.PIPE, creationflags=creationflags)
    time.sleep(2)
  
  @classmethod
  def tearDown(self):
    self.envoy_proc.kill()

  def test_envoy_closes_on_ctrl_c_event(self):
    self.assertTrue(self.envoy_proc.poll() == None)
    ctr_c_signal = None
    if sys.platform == 'win32':
      ctr_c_signal = signal.CTRL_C_EVENT
    else:
      ctr_c_signal = signal.SIGINT
    self.envoy_proc.send_signal(ctr_c_signal)
    time.sleep(2)
    poll = self.envoy_proc.poll()
    self.assertTrue(poll != None)

if __name__ == '__main__':
  unittest.main()