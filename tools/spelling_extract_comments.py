#!/usr/bin/env python

import sys
import re
import codecs
import os

COMMENT_RE = re.compile(r'//.*?$|/\*.*?\*/', re.DOTALL | re.MULTILINE)
TODO_RE = re.compile(r'TODO *\([^)]+\):? ', re.MULTILINE)
CAMEL_CASE_RE = re.compile(r'([a-z])([A-Z])', re.MULTILINE)
BINARY_CODINGS_RE = re.compile(r'[A-Za-z0-9/+=]{16,}', re.MULTILINE)
NONALPHA_RE = re.compile(r'[0-9/+=]')

# TODO(zuercher): it would be great to somehow track the source of each word so that we could
# produce grep-like output containing the file/line with the misspelling.

# Extract // and /* */ comments.
def find_comments(text):
    result = COMMENT_RE.findall(text)
    return result

# Strip out TODO comments.
def strip_todo(text):
    match = TODO_RE.search(text)
    if match is not None:
        text = text[:match.start()] + text[match.end():]
    return text

# Split "Camel"Case" to "Camel Case"
def split_camel_case(text):
    result = ""
    last = 0
    for m in CAMEL_CASE_RE.finditer(text):
        result = result + text[last:m.start()+1] + ' ' + text[m.end()-1:m.end()]
        last = m.end()
    return result + text[last:]

# Ignore hex or base64 strings 16 characters or more in length.
def ignore_binary_codings(text):
    result = ""
    last = 0

    for m in BINARY_CODINGS_RE.finditer(text):
        # only strip if the strings contains at least one non-alpha base64 digit
        if NONALPHA_RE.search(m.group()):
            result = result + text[last:m.start()]
            last = m.end()
    return result + text[last:]

def print_command(filename):
    codefile = codecs.open(filename, 'r', 'utf-8')
    lines = codefile.read()
    codefile.close()

    comments = find_comments(lines)
    for comment in comments:
        if comment[0:2] == "//":
            comment = comment[2:]
        else:
            comment = comment[2:-2]

        comment = strip_todo(comment.strip())
        comment = ignore_binary_codings(comment)
        comment = split_camel_case(comment)
        if len(comment) != 0:
            print(comment.encode('utf-8'))

'''
Usage:
    python spelling_extract_comments.py <file ...>
'''
if __name__ == "__main__":
    for name in sys.argv[1:]:
        try:
            print_command(name)
        except:
            sys.stderr.write("failed to parse " + name + os.linesep)
