import logging
import subprocess

# Echoes and runs an OS command, returning exit status and the captured
# stdout+stderr as a string array.
def runCommand(command):
  stdout = []
  status = 0
  try:
    out = subprocess.check_output(command, shell=True, stderr=subprocess.STDOUT).strip()
    if out:
      stdout = out.decode('utf-8').split("\n")
  except subprocess.CalledProcessError as e:
    status = e.returncode
    for line in e.output.splitlines():
      stdout.append(line)
  logging.info("%s" % command)
  return status, stdout