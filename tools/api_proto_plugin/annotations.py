"""Envoy API annotations."""

from collections import namedtuple

import re

# Key-value annotation regex.
ANNOTATION_REGEX = re.compile('\[#([\w-]+?):\s*(.*?)\](\s?)', re.DOTALL)

# Page/section titles with special prefixes in the proto comments
DOC_TITLE_ANNOTATION = 'protodoc-title'

# When documenting an extension, this should be used to specify the qualified
# name that the extension registers as in the static registry, e.g.
# envoy.filters.network.http_connection_manager.
EXTENSION_ANNOTATION = 'extension'

# Not implemented yet annotation on leading comments, leading to hiding of
# field.
NOT_IMPLEMENTED_HIDE_ANNOTATION = 'not-implemented-hide'

# For large protos, place a comment at the top that specifies the next free field number.
NEXT_FREE_FIELD_ANNOTATION = 'next-free-field'

# Comment that allows for easy searching for things that need cleaning up in the next major
# API version.
NEXT_MAJOR_VERSION_ANNOTATION = 'next-major-version'

# Comment. Just used for adding text that will not go into the docs at all.
COMMENT_ANNOTATION = 'comment'

VALID_ANNOTATIONS = set([
    DOC_TITLE_ANNOTATION,
    EXTENSION_ANNOTATION,
    NOT_IMPLEMENTED_HIDE_ANNOTATION,
    NEXT_FREE_FIELD_ANNOTATION,
    NEXT_MAJOR_VERSION_ANNOTATION,
    COMMENT_ANNOTATION,
])

# These can propagate from file scope to message/enum scope (and be overridden).
INHERITED_ANNOTATIONS = set([
    # Nothing here right now, this used to be PROTO_STATUS_ANNOTATION. Retaining
    # this capability for potential future use.
])


class AnnotationError(Exception):
  """Base error class for the annotations module."""


def ExtractAnnotations(s, inherited_annotations=None):
  """Extract annotations map from a given comment string.

  Args:
    s: string that may contains annotations.
    inherited_annotations: annotation map from file-level inherited annotations
      (or None) if this is a file-level comment.

  Returns:
    Annotation map.
  """
  annotations = {
      k: v for k, v in (inherited_annotations or {}).items() if k in INHERITED_ANNOTATIONS
  }
  # Extract annotations.
  groups = re.findall(ANNOTATION_REGEX, s)
  for group in groups:
    annotation = group[0]
    if annotation not in VALID_ANNOTATIONS:
      raise AnnotationError('Unknown annotation: %s' % annotation)
    annotations[group[0]] = group[1].lstrip()
  return annotations


def XformAnnotation(s, annotation_xforms):
  """Return transformed string with annotation transformers.

  The annotation will be replaced with the new value returned by the transformer.
  If the transformer returns None, then the annotation will be removed.
  If the annotation presented in transformers doesn't exist in the original string,
  a new annotation will be appended to the end of string.

  Args:
    annotation_xforms: a dict of transformers for annotations.

  Returns:
    transformed string.
  """
  present_annotations = set()

  def xform(match):
    annotation, content, trailing = match.groups()
    present_annotations.add(annotation)
    annotation_xform = annotation_xforms.get(annotation)
    if annotation_xform:
      value = annotation_xform(annotation)
      return '[#%s: %s]%s' % (annotation, value, trailing) if value is not None else ''
    else:
      return match.group(0)

  def append(s, annotation, content):
    return '%s [#%s: %s]\n' % (s, annotation, content)

  xformed = re.sub(ANNOTATION_REGEX, xform, s)
  for annotation, xform in sorted(annotation_xforms.items()):
    if annotation not in present_annotations:
      value = xform(None)
      if value is not None:
        xformed = append(xformed, annotation, value)
  return xformed


def WithoutAnnotations(s):
  return re.sub(ANNOTATION_REGEX, '', s)
