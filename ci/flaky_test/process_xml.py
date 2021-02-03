#!/usr/bin/env python3

import subprocess
import os
import xml.etree.ElementTree as ET
import slack
import sys
import ssl

well_known_timeouts = [60, 300, 900, 3600]
section_delimiter = "---------------------------------------------------------------------------------------------------\n"


# Returns a boolean indicating if a test passed.
def didTestPass(file):
  tree = ET.parse(file)
  root = tree.getroot()
  for testsuite in root:
    if testsuite.attrib['failures'] != '0' or testsuite.attrib['errors'] != '0':
      return False
  return True


# Returns a pretty-printed string of a test case failure.
def printTestCaseFailure(testcase, testsuite, failure_msg, log_path):
  ret = "Test flake details:\n"
  ret += "- Test suite:\t{}\n".format(testsuite)
  ret += "- Test case:\t{}\n".format(testcase)
  ret += "- Log path:\t{}\n".format(log_path)
  ret += "- Details:\n"
  for line in failure_msg.splitlines():
    ret += "\t" + line + "\n"
  ret += section_delimiter + "\n"
  return ret


# Returns a pretty-printed string of a test suite error, such as an exception or a timeout.
def printTestSuiteError(testsuite, testcase, log_path, duration, time, error_msg, output):
  ret = "Test flake details:\n"
  ret += "- Test suite:\t{}\n".format(testsuite)
  ret += "- Test case:\t{}\n".format(testcase)
  ret += "- Log path:\t{}\n".format(log_path)

  errno_string = os.strerror(int(error_msg.split(' ')[-1]))
  ret += "- Error:\t{} ({})\n".format(error_msg.capitalize(), errno_string)

  if duration == time and duration in well_known_timeouts:
    ret += "- Note:\t\tThis error is likely a timeout (test duration == {}, a well known timeout value).\n".format(
        duration)

  # If there's a call stack, print it. Otherwise, attempt to print the most recent,
  # relevant lines.
  output = output.rstrip('\n')
  traceback_index = output.rfind('Traceback (most recent call last)')

  if traceback_index != -1:
    ret += "- Relevant snippet:\n"
    for line in output[traceback_index:].splitlines():
      ret += "\t" + line + "\n"
  else:
    # No traceback found. Attempt to print the most recent snippet from the last test case.
    max_snippet_size = 20
    last_testcase_index = output.rfind('[ RUN      ]')
    output_lines = output[last_testcase_index:].splitlines()
    num_lines_to_print = min(len(output_lines), max_snippet_size)

    ret += "- Last {} line(s):\n".format(num_lines_to_print)
    for line in output_lines[-num_lines_to_print:]:
      ret += "\t" + line + "\n"

  ret += "\n" + section_delimiter + "\n"

  return ret


# Parses a test suite error, such as an exception or a timeout, and returns a pretty-printed
# string of the error. This function is dependent on the structure of the XML and the contents
# of the test log and will need to be adjusted should those change.
def parseAndPrintTestSuiteError(testsuite, log_path):
  error_msg = ""
  test_duration = 0
  test_time = 0
  last_testsuite = testsuite.attrib['name']
  last_testcase = testsuite.attrib['name']
  test_output = ""

  # Test suites with errors are expected to have 2 children elements: a generic testcase tag
  # with the runtimes and a child containing the error message, and another with the entire
  # output of the test suite.
  for testcase in testsuite:
    if testcase.tag == "testcase":
      test_duration = int(testcase.attrib['duration'])
      test_time = int(testcase.attrib['time'])

      for child in testcase:
        if child.tag == "error":
          error_msg = child.attrib['message']
    elif testcase.tag == "system-out":
      test_output = testcase.text

      # For test suites with errors like this one, the test suite and test case names were not
      # parsed into the XML metadata. Here we attempt to extract those names from the log by
      # finding the last test case to run. The expected format of that is:
      #     "[ RUN      ] <TestParams>/<TestSuite>.<TestCase>\n".
      last_test_fullname = test_output.split('[ RUN      ]')[-1].splitlines()[0]
      last_testsuite = last_test_fullname.split('/')[1].split('.')[0]
      last_testcase = last_test_fullname.split('.')[1]

  if error_msg != "":
    return printTestSuiteError(last_testsuite, last_testcase, log_path, test_duration, test_time,
                               error_msg, test_output)

  return ""


# Parses a failed test's XML, adds any flaky tests found to the visited set, and returns a
# well-formatted string describing all failures and errors.
def parseXML(file, visited):
  # This is dependent on the fact that log files reside in the same directory
  # as their corresponding xml files.
  log_file = file.split('.')
  log_file_path = ""
  for token in log_file[:-1]:
    log_file_path += token
  log_file_path += ".log"

  tree = ET.parse(file)
  root = tree.getroot()

  # This loop is dependent on the structure of xml file emitted for test runs.
  # Should this change in the future, appropriate adjustments need to be made.
  ret = ""
  for testsuite in root:
    if testsuite.attrib['failures'] != '0':
      for testcase in testsuite:
        for failure_msg in testcase:
          if (testcase.attrib['name'], testsuite.attrib['name']) not in visited:
            ret += printTestCaseFailure(testcase.attrib['name'], testsuite.attrib['name'],
                                        failure_msg.text, log_file_path)
            visited.add((testcase.attrib['name'], testsuite.attrib['name']))
    elif testsuite.attrib['errors'] != '0':
      # If an unexpected error occurred, such as an exception or a timeout, the test suite was
      # likely not parsed into XML properly, including the suite's name and the test case that
      # caused the error. More parsing is needed to extract details about the error.
      if (testsuite.attrib['name'], testsuite.attrib['name']) not in visited:
        ret += parseAndPrintTestSuiteError(testsuite, log_file_path)
        visited.add((testsuite.attrib['name'], testsuite.attrib['name']))

  return ret


# The following function links the filepath of 'test.xml' (the result for the last attempt) with
# that of its 'attempt_n.xml' file and stores it in a dictionary for easy lookup.
def processFindOutput(f, problematic_tests):
  for line in f:
    lineList = line.split('/')
    filepath = ""
    for i in range(len(lineList)):
      if i >= len(lineList) - 2:
        break
      filepath += lineList[i] + "/"
    filepath += "test.xml"
    problematic_tests[filepath] = line.strip('\n')


# Returns helpful information on the run using Git.
# Should Git change the output of the used commands in the future,
# this will likely need adjustments as well.
def getGitInfo(CI_TARGET):
  ret = ""

  if CI_TARGET != "":
    ret += "Target:\t\t{}\n".format(CI_TARGET)

  if os.getenv('SYSTEM_STAGEDISPLAYNAME') and os.getenv('SYSTEM_STAGEJOBNAME'):
    ret += "Stage:\t\t{} {}\n".format(os.environ['SYSTEM_STAGEDISPLAYNAME'],
                                      os.environ['SYSTEM_STAGEJOBNAME'])

  if os.getenv('BUILD_REASON') == "PullRequest" and os.getenv('SYSTEM_PULLREQUEST_PULLREQUESTID'):
    ret += "Pull request:\t{}/pull/{}\n".format(os.environ['REPO_URI'],
                                                os.environ['SYSTEM_PULLREQUEST_PULLREQUESTID'])
  elif os.getenv('BUILD_REASON'):
    ret += "Build reason:\t{}\n".format(os.environ['BUILD_REASON'])

  output = subprocess.check_output(['git', 'log', '--format=%H', '-n', '1'], encoding='utf-8')
  ret += "Commmit:\t{}/commit/{}".format(os.environ['REPO_URI'], output)

  build_id = os.environ['BUILD_URI'].split('/')[-1]
  ret += "CI results:\thttps://dev.azure.com/cncf/envoy/_build/results?buildId=" + build_id + "\n"

  ret += "\n"

  remotes = subprocess.check_output(['git', 'remote'], encoding='utf-8').splitlines()

  if ("origin" in remotes):
    output = subprocess.check_output(['git', 'remote', 'get-url', 'origin'], encoding='utf-8')
    ret += "Origin:\t\t{}".format(output.replace('.git', ''))

  if ("upstream" in remotes):
    output = subprocess.check_output(['git', 'remote', 'get-url', 'upstream'], encoding='utf-8')
    ret += "Upstream:\t{}".format(output.replace('.git', ''))

  output = subprocess.check_output(['git', 'describe', '--all'], encoding='utf-8')
  ret += "Latest ref:\t{}".format(output)

  ret += "\n"

  ret += "Last commit:\n"
  output = subprocess.check_output(['git', 'show', '-s'], encoding='utf-8')
  for line in output.splitlines():
    ret += "\t" + line + "\n"

  ret += section_delimiter

  return ret


if __name__ == "__main__":
  CI_TARGET = ""
  if len(sys.argv) == 2:
    CI_TARGET = sys.argv[1]

  if os.getenv('TEST_TMPDIR') and os.getenv('REPO_URI') and os.getenv("BUILD_URI"):
    os.environ["TMP_OUTPUT_PROCESS_XML"] = os.getenv("TEST_TMPDIR") + "/tmp_output_process_xml.txt"
  else:
    print("Set the env variables TEST_TMPDIR and REPO_URI first.")
    sys.exit(0)

  find_dir = "{}/**/**/**/**/bazel-testlogs/".format(os.environ['TEST_TMPDIR']).replace('\\', '/')
  if CI_TARGET == "MacOS":
    find_dir = '${TEST_TMPDIR}/'
  os.system(
      'sh -c "/usr/bin/find {} -name attempt_*.xml > ${{TMP_OUTPUT_PROCESS_XML}}"'.format(find_dir))

  # All output of find command should be either failed or flaky tests, as only then will
  # a test be rerun and have an 'attempt_n.xml' file. problematic_tests holds a lookup
  # table between the most recent run's xml filepath and the original attempt's failed xml
  # filepath.
  problematic_tests = {}
  with open(os.environ['TMP_OUTPUT_PROCESS_XML'], 'r+') as f:
    processFindOutput(f, problematic_tests)

  # The logic here goes as follows: If there is a test suite that has run multiple times,
  # which produces attempt_*.xml files, it means that the end result of that test
  # is either flaky or failed. So if we find that the last run of the test succeeds
  # we know for sure that this is a flaky test.
  has_flaky_test = False
  failure_output = ""
  flaky_tests_visited = set()
  for k in problematic_tests.keys():
    if didTestPass(k):
      has_flaky_test = True
      failure_output += parseXML(problematic_tests[k], flaky_tests_visited)

  if has_flaky_test:
    output_msg = "``` \n" + getGitInfo(CI_TARGET) + "\n" + failure_output + "``` \n"

    if os.getenv("SLACK_TOKEN"):
      SLACKTOKEN = os.environ["SLACK_TOKEN"]
      ssl_context = ssl.create_default_context()
      ssl_context.check_hostname = False
      ssl_context.verify_mode = ssl.CERT_NONE
      # Due to a weird interaction between `websocket-client` and Slack client
      # we need to set the ssl context. See `slackapi/python-slack-sdk/issues/334`
      client = slack.WebClient(token=SLACKTOKEN, ssl=ssl_context)
      client.chat_postMessage(channel='test-flaky', text=output_msg, as_user="true")
    else:
      print(output_msg)
  else:
    print('No flaky tests found.\n')

  os.remove(os.environ["TMP_OUTPUT_PROCESS_XML"])
