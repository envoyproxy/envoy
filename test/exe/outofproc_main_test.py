
#!/usr/bin/python

import os
import time
import subprocess
import unittest

class OutOfProcEnvoyTests(unittest.TestCase):
  """
  We test that Envoy process behaves correctly.
  """

  @classmethod
  def setUpClass(cls):
    envoy_path = os.path.join(os.getenv('TEST_SRCDIR'), '/envoy/source/exe/envoy-static')
    envoy_config = os.path.join(os.getenv('TEST_SRCDIR'), '/test/config/integration/server.yaml')
    envoy_cl = envoy_path + " " + "--config-path" + " " +  envoy_config
    try:  # Windows-specific.
      creationflags = subprocess.CREATE_NEW_PROCESS_GROUP
    except AttributeError:
      creationflags = 0
    self.envoy_proc = subprocess.Popen(envoy_cl, stdin=subprocess.PIPE, creationflags=creationflags)
    time.sleep(2)
  
  @classmethod
  def tearDownClass(cls):
    self.envoy_proc.kill()

  @classmethod
  def VerifyEnvoyRunning(self):
    self.assertTrue(self.envoy_proc.poll() is None)

  def test_envoy_closes_on_ctrl_c_event(self):
    self.VerifyEnvoyRunning()
    self.envoy_proc.send_signal(CTRL_C_EVENT)
    poll = self.envoy_proc.poll()
    self.assertTrue(poll != None)

if __name__ == '__main__':
  unittest.main()