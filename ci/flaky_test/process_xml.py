#!/usr/bin/env python3

import subprocess
import os
import xml.etree.ElementTree as ET
import slack
import sys


# Check if a test suite reports failure.
def checkTestStatus(file):
  tree = ET.parse(file)

  root = tree.getroot()

  for testsuite in root:
    if (testsuite.attrib['failures'] != '0'):
      return False
  return True


def parseXML(file, visited):
  log_file = file.split('.')
  log_file_path = ""

  # This is dependent on the fact that log files reside in the same directory
  # as their corresponding xml files.
  for token in log_file[:-1]:
    log_file_path += token
  log_file_path += ".log"

  tree = ET.parse(file)

  root = tree.getroot()
  ret = ""

  # This loop is dependent on the structure of xml file emitted for test runs.
  # Should this change in the future, appropriate adjustments need to made.
  for testsuite in root:
    if (testsuite.attrib['failures'] != '0'):
      for testcase in testsuite:
        for failure_msg in testcase:
          if (testcase.attrib['name'], testsuite.attrib['name']) not in visited:
            ret += "-----------------------Flaky Testcase: {} in TestSuite: {} -----------------------\n".format(
                testcase.attrib['name'], testsuite.attrib['name'])
            ret += log_file_path + "\n" + failure_msg.text + "\n"
            visited.add((testcase.attrib['name'], testsuite.attrib['name']))
  return ret


# The following function links the filepath of 'test.xml' (the result for the last attempt) with
# that of its 'attmpt_n.xml' file and stores it in a dictionary for easy lookup.
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


# Prints out helpful information on the run using Git.
# Should Git changes the output of the used commands in the future,
# this will likely need adjustments as well.
def getGitInfo(CI_TARGET):
  ret = ""
  os.system("git remote -v > ${TMP_OUTPUT_PROCESS_XML}")
  os.system("git describe --all >> ${TMP_OUTPUT_PROCESS_XML}")
  os.system("git show >> ${TMP_OUTPUT_PROCESS_XML}")
  f = open(os.environ['TMP_OUTPUT_PROCESS_XML'], 'r+')
  # Fetching the URL from predefined env variable
  envoy_link = os.environ["REPO_URI"]
  for line in [next(f) for x in range(6)]:
    if line.split('/')[0] == 'remotes':
      for token in line.split('/')[1:-1]:
        envoy_link += '/' + token
    ret += line

  ret += "link for additional content: " + envoy_link + " \n"
  ret += "azure build URI: " + os.environ["BUILD_URI"] + " \n"
  if CI_TARGET != "":
    ret += "In " + CI_TARGET + " build\n"
  return ret


if __name__ == "__main__":
  CI_TARGET = ""
  if len(sys.argv) == 2:
    CI_TARGET = sys.argv[1]
  output_msg = "``` \n"
  has_flaky_test = False

  if os.getenv("TEST_TMPDIR") and os.getenv("REPO_URI") and os.getenv("BUILD_URI"):
    os.environ["TMP_OUTPUT_PROCESS_XML"] = os.getenv("TEST_TMPDIR") + "/tmp_output_process_xml.txt"
  else:
    print("set the env variables first")
    sys.exit(0)
  output_msg += getGitInfo(CI_TARGET)

  if CI_TARGET == "MacOS":
    os.system('find ${TEST_TMPDIR}/ -name "attempt_*.xml" > ${TMP_OUTPUT_PROCESS_XML}')
  else:
    os.system(
        'find ${TEST_TMPDIR}/**/**/**/**/bazel-testlogs/ -name "attempt_*.xml" > ${TMP_OUTPUT_PROCESS_XML}'
    )

  f = open(os.environ['TMP_OUTPUT_PROCESS_XML'], 'r+')
  if f.closed:
    print("cannot open {}".format(os.environ['TMP_OUTPUT_PROCESS_XML']))

  # All output of find command should be either failed or flaky tests, as only then will
  # a test be rerun and have an 'attempt_n.xml' file. problematic_tests holds a lookup
  # table between the last_attempt xml filepath and the failed previous attempt filepath.
  problematic_tests = {}
  processFindOutput(f, problematic_tests)

  # Needed to make sure no duplicate flaky tests are going to be reported.
  visited = set()

  # The logic here goes as follows: If there is a test suite that has run multiple times,
  # which produces attempt_*.xml files, it means that the end result of that test
  # is either flaky or failed. So if we find that the last run of the test succeeds
  # we know for sure that this is a flaky test.
  for k in problematic_tests.keys():
    if checkTestStatus(k):
      has_flaky_test = True
      output_msg += parseXML(problematic_tests[k], visited)
  output_msg += "``` \n"

  if has_flaky_test:
    if os.getenv("SLACK_TOKEN"):
      SLACKTOKEN = os.environ["SLACK_TOKEN"]
      client = slack.WebClient(SLACKTOKEN)
      client.chat_postMessage(channel='test-flaky', text=output_msg, as_user="true")
    else:
      print(output_msg)

  os.remove(os.environ["TMP_OUTPUT_PROCESS_XML"])
