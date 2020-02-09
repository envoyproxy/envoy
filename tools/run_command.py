import subprocess


# Echoes and runs an OS command, returning exit status and the captured
# stdout and stderr as a string array.
def runCommand(command):
  proc = subprocess.run([command], shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

  return proc.returncode, proc.stdout.decode('utf-8').split('\n'), proc.stderr.decode(
      'utf-8').split('\n')
