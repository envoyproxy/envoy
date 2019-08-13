#!/usr/bin/python

# Main library file containing all the protocol generation logic.


def generate_main_code(type, main_header_file, resolver_cc_file, input_files):
  """
  Main code generator.

  Takes input files and processes them into structures representing a Kafka message (request or
  response).

  These responses are then used to create:
  - main_header_file - contains definitions of Kafka structures and their deserializers
  - resolver_cc_file - contains request api key & version mapping to deserializer (from header file)
  """
  # Parse provided input files.
  messages = parse_messages(input_files)

  complex_type_template = RenderingHelper.get_template('complex_type_template.j2')
  parsers_template = RenderingHelper.get_template("%s_parser.j2" % type)

  main_header_contents = ''

  for message in messages:
    # For each child structure that is used by request/response, render its matching C++ code.
    for dependency in message.declaration_chain:
      main_header_contents += complex_type_template.render(complex_type=dependency)
    # Each top-level structure (e.g. FetchRequest/FetchResponse) needs corresponding parsers.
    main_header_contents += parsers_template.render(complex_type=message)

  # Full file with headers, namespace declaration etc.
  template = RenderingHelper.get_template("%ss_h.j2" % type)
  contents = template.render(contents=main_header_contents)

  # Generate main header file.
  with open(main_header_file, 'w') as fd:
    fd.write(contents)

  template = RenderingHelper.get_template("kafka_%s_resolver_cc.j2" % type)
  contents = template.render(message_types=messages)

  # Generate ...resolver.cc file.
  with open(resolver_cc_file, 'w') as fd:
    fd.write(contents)


def generate_test_code(type, header_test_cc_file, codec_test_cc_file, input_files):
  """
  Test code generator.

  Takes input files and processes them into structures representing a Kafka message (request or
  response).

  These responses are then used to create:
  - header_test_cc_file - tests for basic message serialization deserialization
  - codec_test_cc_file - tests involving codec and Request/ResponseParserResolver
  """
  # Parse provided input files.
  messages = parse_messages(input_files)

  # Generate header-test file.
  template = RenderingHelper.get_template("%ss_test_cc.j2" % type)
  contents = template.render(message_types=messages)
  with open(header_test_cc_file, 'w') as fd:
    fd.write(contents)

  # Generate codec-test file.
  template = RenderingHelper.get_template("%s_codec_%s_test_cc.j2" % (type, type))
  contents = template.render(message_types=messages)
  with open(codec_test_cc_file, 'w') as fd:
    fd.write(contents)


def parse_messages(input_files):
  """
  Parse request/response structures from provided input files.
  """
  import re
  import json

  messages = []
  # For each specification file, remove comments, and parse the remains.
  for input_file in input_files:
    with open(input_file, 'r') as fd:
      raw_contents = fd.read()
      without_comments = re.sub(r'//.*\n', '', raw_contents)
      message_spec = json.loads(without_comments)
      message = parse_top_level_element(message_spec)
      messages.append(message)

  # Sort messages by api_key.
  messages.sort(key=lambda x: x.get_extra('api_key'))
  return messages


def parse_top_level_element(spec):
  """
  Parse a given structure into a request/response.
  Request/response is just a complex type, that has name & version information kept in differently
  named fields, compared to sub-structures in a message.
  """
  type_name = spec['name']
  versions = Statics.parse_version_string(spec['validVersions'], 2 << 16 - 1)
  return parse_complex_type(type_name, spec, versions).with_extra('api_key', spec['apiKey'])


def parse_complex_type(type_name, field_spec, versions):
  """
  Parse given complex type, returning a structure that holds its name, field specification and
  allowed versions.
  """
  fields = []
  for child_field in field_spec['fields']:
    child = parse_field(child_field, versions[-1])
    fields.append(child)
  return Complex(type_name, fields, versions)


def parse_field(field_spec, highest_possible_version):
  """
  Parse given field, returning a structure holding the name, type, and versions when this field is
  actually used (nullable or not). Obviously, field cannot be used in version higher than its
  type's usage.
  """
  version_usage = Statics.parse_version_string(field_spec['versions'], highest_possible_version)
  version_usage_as_nullable = Statics.parse_version_string(
      field_spec['nullableVersions'],
      highest_possible_version) if 'nullableVersions' in field_spec else range(-1)
  parsed_type = parse_type(field_spec['type'], field_spec, highest_possible_version)
  return FieldSpec(field_spec['name'], parsed_type, version_usage, version_usage_as_nullable)


def parse_type(type_name, field_spec, highest_possible_version):
  """
  Parse a given type element - returns an array type, primitive (e.g. uint32_t) or complex one.
  """
  if (type_name.startswith('[]')):
    # In spec files, array types are defined as `[]underlying_type` instead of having its own
    # element with type inside.
    underlying_type = parse_type(type_name[2:], field_spec, highest_possible_version)
    return Array(underlying_type)
  else:
    if (type_name in Primitive.PRIMITIVE_TYPE_NAMES):
      return Primitive(type_name, field_spec.get('default'))
    else:
      versions = Statics.parse_version_string(field_spec['versions'], highest_possible_version)
      return parse_complex_type(type_name, field_spec, versions)


class Statics:

  @staticmethod
  def parse_version_string(raw_versions, highest_possible_version):
    """
    Return integer range that corresponds to version string in spec file.
    """
    if raw_versions.endswith('+'):
      return range(int(raw_versions[:-1]), highest_possible_version + 1)
    else:
      if '-' in raw_versions:
        tokens = raw_versions.split('-', 1)
        return range(int(tokens[0]), int(tokens[1]) + 1)
      else:
        single_version = int(raw_versions)
        return range(single_version, single_version + 1)


class FieldList:
  """
  List of fields used by given entity (request or child structure) in given message version
  (as fields get added or removed across versions).
  """

  def __init__(self, version, fields):
    self.version = version
    self.fields = fields

  def used_fields(self):
    """
    Return list of fields that are actually used in this version of structure.
    """
    return filter(lambda x: x.used_in_version(self.version), self.fields)

  def constructor_signature(self):
    """
    Return constructor signature.
    Multiple versions of the same structure can have identical signatures (due to version bumps in
    Kafka).
    """
    parameter_spec = map(lambda x: x.parameter_declaration(self.version), self.used_fields())
    return ', '.join(parameter_spec)

  def constructor_init_list(self):
    """
    Renders member initialization list in constructor.
    Takes care of potential optional<T> conversions (as field could be T in V1, but optional<T>
    in V2).
    """
    init_list = []
    for field in self.fields:
      if field.used_in_version(self.version):
        if field.is_nullable():
          if field.is_nullable_in_version(self.version):
            # Field is optional<T>, and the parameter is optional<T> in this version.
            init_list_item = '%s_{%s}' % (field.name, field.name)
            init_list.append(init_list_item)
          else:
            # Field is optional<T>, and the parameter is T in this version.
            init_list_item = '%s_{absl::make_optional(%s)}' % (field.name, field.name)
            init_list.append(init_list_item)
        else:
          # Field is T, so parameter cannot be optional<T>.
          init_list_item = '%s_{%s}' % (field.name, field.name)
          init_list.append(init_list_item)
      else:
        # Field is not used in this version, so we need to put in default value.
        init_list_item = '%s_{%s}' % (field.name, field.default_value())
        init_list.append(init_list_item)
      pass
    return ', '.join(init_list)

  def field_count(self):
    return len(list(self.used_fields()))

  def example_value(self):
    return ', '.join(map(lambda x: x.example_value_for_test(self.version), self.used_fields()))


class FieldSpec:
  """
  Represents a field present in a structure (request, or child structure thereof).
  Contains name, type, and versions when it is used (nullable or not).
  """

  def __init__(self, name, type, version_usage, version_usage_as_nullable):
    import re
    separated = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', name)
    self.name = re.sub('([a-z0-9])([A-Z])', r'\1_\2', separated).lower()
    self.type = type
    self.version_usage = version_usage
    self.version_usage_as_nullable = version_usage_as_nullable

  def is_nullable(self):
    return len(self.version_usage_as_nullable) > 0

  def is_nullable_in_version(self, version):
    """
    Whether the field is nullable in given version.
    Fields can be non-nullable in earlier versions.
    See https://github.com/apache/kafka/tree/2.2.0-rc0/clients/src/main/resources/common/message#nullable-fields
    """
    return version in self.version_usage_as_nullable

  def used_in_version(self, version):
    return version in self.version_usage

  def field_declaration(self):
    if self.is_nullable():
      return 'absl::optional<%s> %s' % (self.type.name, self.name)
    else:
      return '%s %s' % (self.type.name, self.name)

  def parameter_declaration(self, version):
    if self.is_nullable_in_version(version):
      return 'absl::optional<%s> %s' % (self.type.name, self.name)
    else:
      return '%s %s' % (self.type.name, self.name)

  def default_value(self):
    if self.is_nullable():
      return '{%s}' % self.type.default_value()
    else:
      return str(self.type.default_value())

  def example_value_for_test(self, version):
    if self.is_nullable():
      return 'absl::make_optional<%s>(%s)' % (self.type.name,
                                              self.type.example_value_for_test(version))
    else:
      return str(self.type.example_value_for_test(version))

  def deserializer_name_in_version(self, version):
    if self.is_nullable_in_version(version):
      return 'Nullable%s' % self.type.deserializer_name_in_version(version)
    else:
      return self.type.deserializer_name_in_version(version)

  def is_printable(self):
    return self.type.is_printable()


class TypeSpecification:

  def deserializer_name_in_version(self, version):
    """
    Renders the deserializer name of given type, in message with given version.
    """
    raise NotImplementedError()

  def default_value(self):
    """
    Returns a default value for given type.
    """
    raise NotImplementedError()

  def example_value_for_test(self, version):
    raise NotImplementedError()

  def is_printable(self):
    raise NotImplementedError()


class Array(TypeSpecification):
  """
  Represents array complex type.
  To use instance of this type, it is necessary to declare structures required by self.underlying
  (e.g. to use Array<Foo>, we need to have `struct Foo {...}`).
  """

  def __init__(self, underlying):
    self.underlying = underlying
    self.declaration_chain = self.underlying.declaration_chain

  @property
  def name(self):
    return 'std::vector<%s>' % self.underlying.name

  def deserializer_name_in_version(self, version):
    return 'ArrayDeserializer<%s, %s>' % (self.underlying.name,
                                          self.underlying.deserializer_name_in_version(version))

  def default_value(self):
    return 'std::vector<%s>{}' % (self.underlying.name)

  def example_value_for_test(self, version):
    return 'std::vector<%s>{ %s }' % (self.underlying.name,
                                      self.underlying.example_value_for_test(version))

  def is_printable(self):
    return self.underlying.is_printable()


class Primitive(TypeSpecification):
  """
  Represents a Kafka primitive value.
  """

  PRIMITIVE_TYPE_NAMES = ['bool', 'int8', 'int16', 'int32', 'int64', 'string', 'bytes']

  KAFKA_TYPE_TO_ENVOY_TYPE = {
      'string': 'std::string',
      'bool': 'bool',
      'int8': 'int8_t',
      'int16': 'int16_t',
      'int32': 'int32_t',
      'int64': 'int64_t',
      'bytes': 'Bytes',
  }

  KAFKA_TYPE_TO_DESERIALIZER = {
      'string': 'StringDeserializer',
      'bool': 'BooleanDeserializer',
      'int8': 'Int8Deserializer',
      'int16': 'Int16Deserializer',
      'int32': 'Int32Deserializer',
      'int64': 'Int64Deserializer',
      'bytes': 'BytesDeserializer',
  }

  # See https://github.com/apache/kafka/tree/trunk/clients/src/main/resources/common/message#deserializing-messages
  KAFKA_TYPE_TO_DEFAULT_VALUE = {
      'string': '""',
      'bool': 'false',
      'int8': '0',
      'int16': '0',
      'int32': '0',
      'int64': '0',
      'bytes': '{}',
  }

  # Custom values that make test code more readable.
  KAFKA_TYPE_TO_EXAMPLE_VALUE_FOR_TEST = {
      'string': '"string"',
      'bool': 'false',
      'int8': 'static_cast<int8_t>(8)',
      'int16': 'static_cast<int16_t>(16)',
      'int32': 'static_cast<int32_t>(32)',
      'int64': 'static_cast<int64_t>(64)',
      'bytes': 'Bytes({0, 1, 2, 3})',
  }

  def __init__(self, name, custom_default_value):
    self.original_name = name
    self.name = Primitive.compute(name, Primitive.KAFKA_TYPE_TO_ENVOY_TYPE)
    self.custom_default_value = custom_default_value
    self.declaration_chain = []
    self.deserializer_name = Primitive.compute(name, Primitive.KAFKA_TYPE_TO_DESERIALIZER)

  @staticmethod
  def compute(name, map):
    if name in map:
      return map[name]
    else:
      raise ValueError(name)

  def deserializer_name_in_version(self, version):
    return self.deserializer_name

  def default_value(self):
    if self.custom_default_value is not None:
      return self.custom_default_value
    else:
      return Primitive.compute(self.original_name, Primitive.KAFKA_TYPE_TO_DEFAULT_VALUE)

  def example_value_for_test(self, version):
    return Primitive.compute(self.original_name, Primitive.KAFKA_TYPE_TO_EXAMPLE_VALUE_FOR_TEST)

  def is_printable(self):
    return self.name not in ['Bytes']


class Complex(TypeSpecification):
  """
  Represents a complex type (multiple types aggregated into one).
  This type gets mapped to a C++ struct.
  """

  def __init__(self, name, fields, versions):
    self.name = name
    self.fields = fields
    self.versions = versions
    self.declaration_chain = self.__compute_declaration_chain()
    self.attributes = {}

  def __compute_declaration_chain(self):
    """
    Computes all dependencies, what means all non-primitive types used by this type.
    They need to be declared before this struct is declared.
    """
    result = []
    for field in self.fields:
      result.extend(field.type.declaration_chain)
    result.append(self)
    return result

  def with_extra(self, key, value):
    self.attributes[key] = value
    return self

  def get_extra(self, key):
    return self.attributes[key]

  def compute_constructors(self):
    """
    Field lists for different versions may not differ (as Kafka can bump version without any
    changes). But constructors need to be unique, so we need to remove duplicates if the signatures
    match.
    """
    signature_to_constructor = {}
    for field_list in self.compute_field_lists():
      signature = field_list.constructor_signature()
      constructor = signature_to_constructor.get(signature)
      if constructor is None:
        entry = {}
        entry['versions'] = [field_list.version]
        entry['signature'] = signature
        if (len(signature) > 0):
          entry['full_declaration'] = '%s(%s): %s {};' % (self.name, signature,
                                                          field_list.constructor_init_list())
        else:
          entry['full_declaration'] = '%s() {};' % self.name
        signature_to_constructor[signature] = entry
      else:
        constructor['versions'].append(field_list.version)
    return sorted(signature_to_constructor.values(), key=lambda x: x['versions'][0])

  def compute_field_lists(self):
    """
    Return field lists representing each of structure versions.
    """
    field_lists = []
    for version in self.versions:
      field_list = FieldList(version, self.fields)
      field_lists.append(field_list)
    return field_lists

  def deserializer_name_in_version(self, version):
    return '%sV%dDeserializer' % (self.name, version)

  def default_value(self):
    raise NotImplementedError('unable to create default value of complex type')

  def example_value_for_test(self, version):
    field_list = next(fl for fl in self.compute_field_lists() if fl.version == version)
    example_values = map(lambda x: x.example_value_for_test(version), field_list.used_fields())
    return '%s(%s)' % (self.name, ', '.join(example_values))

  def is_printable(self):
    return True


class RenderingHelper:
  """
  Helper for jinja templates.
  """

  @staticmethod
  def get_template(template):
    import jinja2
    import os
    import sys
    # Templates are resolved relatively to main start script, due to main & test templates being
    # stored in different directories.
    env = jinja2.Environment(
        loader=jinja2.FileSystemLoader(searchpath=os.path.dirname(os.path.abspath(sys.argv[0]))))
    return env.get_template(template)
