#!/usr/bin/python

# Main library file containing all the protocol generation logic.


def generate_main_code(type, main_header_file, resolver_cc_file, metrics_header_file, input_files):
    """
  Main code generator.

  Takes input files and processes them into structures representing a Kafka message (request or
  response).

  These responses are then used to create:
  - main_header_file - contains definitions of Kafka structures and their deserializers
  - resolver_cc_file - contains request api key & version mapping to deserializer (from header file)
  - metrics_header_file - contains metrics with names corresponding to messages
  """
    processor = StatefulProcessor(type)
    # Parse provided input files.
    messages = processor.parse_messages(input_files)

    complex_type_template = RenderingHelper.get_template('complex_type_template.j2')
    parsers_template = RenderingHelper.get_template("%s_parser.j2" % type)

    main_header_contents = ''

    for message in messages:
        # For each child structure that is used by request/response, render its matching C++ code.
        dependencies = message.compute_declaration_chain()
        for dependency in dependencies:
            main_header_contents += complex_type_template.render(complex_type=dependency)
        # Each top-level structure (e.g. FetchRequest/FetchResponse) needs corresponding parsers.
        main_header_contents += parsers_template.render(complex_type=message)

    # Full file with headers, namespace declaration etc.
    template = RenderingHelper.get_template("%ss_h.j2" % type)
    contents = template.render(contents=main_header_contents)

    # Generate main header file.
    with open(main_header_file, 'w') as fd:
        fd.write(contents)

    # Generate ...resolver.cc file.
    template = RenderingHelper.get_template("kafka_%s_resolver_cc.j2" % type)
    contents = template.render(message_types=messages)
    with open(resolver_cc_file, 'w') as fd:
        fd.write(contents)

    # Generate ...metrics.h file.
    template = RenderingHelper.get_template("%s_metrics_h.j2" % type)
    contents = template.render(message_types=messages)
    with open(metrics_header_file, 'w') as fd:
        fd.write(contents)


def generate_test_code(
        type, header_test_cc_file, codec_test_cc_file, utilities_cc_file, input_files):
    """
  Test code generator.

  Takes input files and processes them into structures representing a Kafka message (request or
  response).

  These responses are then used to create:
  - header_test_cc_file - tests for basic message serialization deserialization,
  - codec_test_cc_file - tests involving codec and Request/ResponseParserResolver,
  - utilities_cc_file - utilities for creating sample messages.
  """
    processor = StatefulProcessor(type)
    # Parse provided input files.
    messages = processor.parse_messages(input_files)

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

    # Generate utilities file.
    template = RenderingHelper.get_template("%s_utilities_cc.j2" % type)
    contents = template.render(message_types=messages)
    with open(utilities_cc_file, 'w') as fd:
        fd.write(contents)


class StatefulProcessor:
    """
  Helper entity that keeps state during the processing.
  Some state needs to be shared across multiple message types, as we need to handle identical
  sub-type names (e.g. both AlterConfigsRequest & IncrementalAlterConfigsRequest have child
  AlterConfigsResource, what would cause a compile-time error if we were to handle it trivially).
  """

    def __init__(self, type):
        self.type = type
        # Complex types that have been encountered during processing.
        self.known_types = set()
        # Name of parent message type that's being processed right now.
        self.currently_processed_message_type = None
        # Common structs declared in this message type.
        self.common_structs = {}

    def parse_messages(self, input_files):
        """
        Parse request/response structures from provided input files.
        """
        import re
        import json

        messages = []
        # Sort the input files, as the processing is stateful, as we want the same order every time.
        input_files.sort()
        # For each specification file, remove comments, and parse the remains.
        for input_file in input_files:
            try:
                with open(input_file, 'r') as fd:
                    raw_contents = fd.read()
                    without_comments = re.sub(r'\s*//.*\n', '\n', raw_contents)
                    without_empty_newlines = re.sub(
                        r'^\s*$', '', without_comments, flags=re.MULTILINE)
                    # Windows support: see PR 10542 for details.
                    amended = re.sub(r'-2147483648', 'INT32_MIN', without_empty_newlines)
                    message_spec = json.loads(amended)
                    api_key = message_spec['apiKey']
                    # (adam.kotwasinski) ConsumerGroupHeartbeat needs some more changes to parse.
                    if api_key not in [68]:
                        message = self.parse_top_level_element(message_spec)
                        messages.append(message)
            except Exception as e:
                print('could not process %s' % input_file)
                raise

        # Sort messages by api_key.
        messages.sort(key=lambda x: x.get_extra('api_key'))
        return messages

    def parse_top_level_element(self, spec):
        """
    Parse a given structure into a request/response.
    Request/response is just a complex type, that has name & version information kept in differently
    named fields, compared to sub-structures in a message.
    """
        self.currently_processed_message_type = spec['name']

        # Figure out all versions of this message type.
        versions = Statics.parse_version_string(spec['validVersions'], 2 << 16 - 1)

        # Figure out the flexible versions.
        flexible_versions_string = spec.get('flexibleVersions', 'none')
        if 'none' != flexible_versions_string:
            flexible_versions = Statics.parse_version_string(flexible_versions_string, versions[-1])
        else:
            flexible_versions = []

        # Sanity check - all flexible versions need to be versioned.
        if [x for x in flexible_versions if x not in versions]:
            raise ValueError('invalid flexible versions')

        try:
            # In 2.4 some types are declared at top level, and only referenced inside.
            # So let's parse them and store them in state.
            common_structs = spec.get('commonStructs')
            if common_structs is not None:
                for common_struct in reversed(common_structs):
                    common_struct_name = common_struct['name']
                    common_struct_versions = Statics.parse_version_string(
                        common_struct['versions'], versions[-1])
                    parsed_complex = self.parse_complex_type(
                        common_struct_name, common_struct, common_struct_versions)
                    self.common_structs[parsed_complex.name] = parsed_complex

            # Parse the type itself.
            complex_type = self.parse_complex_type(
                self.currently_processed_message_type, spec, versions)
            complex_type.register_flexible_versions(flexible_versions)

            # Request / response types need to carry api key version.
            result = complex_type.with_extra('api_key', spec['apiKey'])
            return result

        finally:
            self.common_structs = {}
            self.currently_processed_message_type = None

    def parse_complex_type(self, type_name, field_spec, versions):
        """
    Parse given complex type, returning a structure that holds its name, field specification and
    allowed versions.
    """
        fields_el = field_spec.get('fields')

        if fields_el is not None:
            fields = []
            for child_field in field_spec['fields']:
                child = self.parse_field(child_field, versions[-1])
                if child is not None:
                    fields.append(child)
            # Some structures share the same name, use request/response as prefix.
            if type_name in ['EntityData', 'EntryData', 'PartitionData', 'PartitionSnapshot',
                             'SnapshotId', 'TopicData', 'TopicSnapshot']:
                type_name = self.type.capitalize() + type_name
            # Some of the types repeat multiple times (e.g. AlterableConfig).
            # In such a case, every second or later occurrence of the same name is going to be prefixed
            # with parent type, e.g. we have AlterableConfig (for AlterConfigsRequest) and then
            # IncrementalAlterConfigsRequestAlterableConfig (for IncrementalAlterConfigsRequest).
            # This keeps names unique, while keeping non-duplicate ones short.
            if type_name not in self.known_types:
                self.known_types.add(type_name)
            else:
                type_name = self.currently_processed_message_type + type_name
                self.known_types.add(type_name)

            return Complex(type_name, fields, versions)

        else:
            return self.common_structs[type_name]

    def parse_field(self, field_spec, highest_possible_version):
        """
    Parse given field, returning a structure holding the name, type, and versions when this field is
    actually used (nullable or not). Obviously, field cannot be used in version higher than its
    type's usage.
    """
        if field_spec.get('tag') is not None:
            return None

        version_usage = Statics.parse_version_string(
            field_spec['versions'], highest_possible_version)
        version_usage_as_nullable = Statics.parse_version_string(
            field_spec['nullableVersions'],
            highest_possible_version) if 'nullableVersions' in field_spec else range(-1)
        parsed_type = self.parse_type(field_spec['type'], field_spec, highest_possible_version)
        return FieldSpec(field_spec['name'], parsed_type, version_usage, version_usage_as_nullable)

    def parse_type(self, type_name, field_spec, highest_possible_version):
        """
        Parse a given type element - returns an array type, primitive (e.g. uint32_t) or complex one.
        """
        if (type_name.startswith('[]')):
            # In spec files, array types are defined as `[]underlying_type` instead of having its own
            # element with type inside.
            underlying_type = self.parse_type(type_name[2:], field_spec, highest_possible_version)
            return Array(underlying_type)
        else:
            if (type_name in Primitive.USABLE_PRIMITIVE_TYPE_NAMES):
                return Primitive(type_name, field_spec.get('default'))
            else:
                versions = Statics.parse_version_string(
                    field_spec['versions'], highest_possible_version)
                return self.parse_complex_type(type_name, field_spec, versions)


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
  (as fields get added or removed across versions and/or they change compaction level).
  """

    def __init__(self, version, uses_compact_fields, fields):
        self.version = version
        self.uses_compact_fields = uses_compact_fields
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
            type_default_value = self.type.default_value()
            # For nullable fields, it's possible to have (Java) null as default value.
            if type_default_value != 'null':
                return '{%s}' % type_default_value
            else:
                return 'absl::nullopt'
        else:
            return str(self.type.default_value())

    def example_value_for_test(self, version):
        if self.is_nullable_in_version(version):
            return 'absl::make_optional<%s>(%s)' % (
                self.type.name, self.type.example_value_for_test(version))
        else:
            return str(self.type.example_value_for_test(version))

    def deserializer_name_in_version(self, version, compact):
        if self.is_nullable_in_version(version):
            return 'Nullable%s' % self.type.deserializer_name_in_version(version, compact)
        else:
            return self.type.deserializer_name_in_version(version, compact)

    def is_printable(self):
        return self.type.is_printable()


class TypeSpecification:

    def compute_declaration_chain(self):
        """
    Computes types that need to be declared before this type can be declared, in C++ sense.
    """
        raise NotImplementedError()

    def deserializer_name_in_version(self, version, compact):
        """
    Renders the deserializer name of given type, in message with given version.
    """
        raise NotImplementedError()

    def default_value(self):
        """
    Returns a default value for given type.
    """
        raise NotImplementedError()

    def has_flexible_handling(self):
        """
    Whether the given type has special encoding when carrying message is using flexible encoding.
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

    @property
    def name(self):
        return 'std::vector<%s>' % self.underlying.name

    def compute_declaration_chain(self):
        # To use an array of type T, we just need to be capable of using type T.
        return self.underlying.compute_declaration_chain()

    def deserializer_name_in_version(self, version, compact):
        # For arrays, deserializer name is (Compact)(Nullable)ArrayDeserializer<ElementDeserializer>.
        element_deserializer_name = self.underlying.deserializer_name_in_version(version, compact)
        return '%sArrayDeserializer<%s>' % ("Compact" if compact else "", element_deserializer_name)

    def default_value(self):
        return 'std::vector<%s>{}' % (self.underlying.name)

    def has_flexible_handling(self):
        return True

    def example_value_for_test(self, version):
        return 'std::vector<%s>{ %s }' % (
            self.underlying.name, self.underlying.example_value_for_test(version))

    def is_printable(self):
        return self.underlying.is_printable()


class Primitive(TypeSpecification):
    """
    Represents a Kafka primitive value.
    """

    USABLE_PRIMITIVE_TYPE_NAMES = [
        'bool', 'int8', 'int16', 'int32', 'int64', 'uint16', 'float64', 'string', 'bytes',
        'records', 'uuid'
    ]

    KAFKA_TYPE_TO_ENVOY_TYPE = {
        'string': 'std::string',
        'bool': 'bool',
        'int8': 'int8_t',
        'int16': 'int16_t',
        'int32': 'int32_t',
        'int64': 'int64_t',
        'uint16': 'uint16_t',
        'float64': 'double',
        'bytes': 'Bytes',
        'records': 'Bytes',
        'uuid': 'Uuid',
        'tagged_fields': 'TaggedFields',
    }

    KAFKA_TYPE_TO_DESERIALIZER = {
        'string': 'StringDeserializer',
        'bool': 'BooleanDeserializer',
        'int8': 'Int8Deserializer',
        'int16': 'Int16Deserializer',
        'int32': 'Int32Deserializer',
        'int64': 'Int64Deserializer',
        'uint16': 'UInt16Deserializer',
        'float64': 'Float64Deserializer',
        'bytes': 'BytesDeserializer',
        'records': 'BytesDeserializer',
        'uuid': 'UuidDeserializer',
        'tagged_fields': 'TaggedFieldsDeserializer',
    }

    KAFKA_TYPE_TO_COMPACT_DESERIALIZER = {
        'string': 'CompactStringDeserializer',
        'bytes': 'CompactBytesDeserializer',
        'records': 'CompactBytesDeserializer'
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
        'uuid': 'Uuid{0, 0}',
        'tagged_fields': 'TaggedFields({})',
    }

    # Custom values that make test code more readable.
    KAFKA_TYPE_TO_EXAMPLE_VALUE_FOR_TEST = {
        'string':
            'static_cast<std::string>("string")',
        'bool':
            'false',
        'int8':
            'static_cast<int8_t>(8)',
        'int16':
            'static_cast<int16_t>(16)',
        'uint16':
            'static_cast<uint16_t>(17)',
        'int32':
            'static_cast<int32_t>(32)',
        'int64':
            'static_cast<int64_t>(64)',
        'float64':
            'static_cast<double>(13.125)',
        'bytes':
            'Bytes({0, 1, 2, 3})',
        'records':
            'Bytes({0, 1, 2, 3})',
        'uuid':
            'Uuid{13, 42}',
        'tagged_fields':
            'TaggedFields{std::vector<TaggedField>{{10, Bytes({1, 2, 3})}, {20, Bytes({4, 5, 6})}}}',
    }

    def __init__(self, name, custom_default_value):
        self.original_name = name
        self.name = Primitive.compute(name, Primitive.KAFKA_TYPE_TO_ENVOY_TYPE)
        self.custom_default_value = Primitive.sanitize_value(self.name, custom_default_value)

    @staticmethod
    def compute(name, map):
        if name in map:
            return map[name]
        else:
            raise ValueError(name)

    @staticmethod
    def sanitize_value(type, arg):
        """
        Unfortunately we cannot print Python True/False straight into C++ code, so we lowercase.
        """
        if arg is None:
            return None
        if 'bool' == type:
            return str(arg).lower()
        else:
            return arg

    def compute_declaration_chain(self):
        # Primitives need no declarations.
        return []

    def deserializer_name_in_version(self, version, compact):
        if compact and self.original_name in Primitive.KAFKA_TYPE_TO_COMPACT_DESERIALIZER.keys():
            return Primitive.compute(
                self.original_name, Primitive.KAFKA_TYPE_TO_COMPACT_DESERIALIZER)
        else:
            return Primitive.compute(self.original_name, Primitive.KAFKA_TYPE_TO_DESERIALIZER)

    def default_value(self):
        if self.custom_default_value is not None:
            return self.custom_default_value
        else:
            return Primitive.compute(self.original_name, Primitive.KAFKA_TYPE_TO_DEFAULT_VALUE)

    def has_flexible_handling(self):
        return self.original_name in ['string', 'bytes', 'records', 'tagged_fields']

    def example_value_for_test(self, version):
        return Primitive.compute(self.original_name, Primitive.KAFKA_TYPE_TO_EXAMPLE_VALUE_FOR_TEST)

    def is_printable(self):
        return self.name not in ['Bytes']


class FieldSerializationSpec():

    def __init__(self, field, versions, compute_size_method_name, encode_method_name):
        self.field = field
        self.versions = versions
        self.compute_size_method_name = compute_size_method_name
        self.encode_method_name = encode_method_name


class Complex(TypeSpecification):
    """
  Represents a complex type (multiple types aggregated into one).
  This type gets mapped to a C++ struct.
  """

    def __init__(self, name, fields, versions):
        self.name = name
        self.fields = fields
        self.versions = versions
        self.flexible_versions = None  # Will be set in 'register_flexible_versions'.
        self.attributes = {}

    def register_flexible_versions(self, flexible_versions):
        # If flexible versions are present, so we need to add placeholder 'tagged_fields' field to
        # *every* type that's used in by this message type.
        for type in self.compute_declaration_chain():
            type.flexible_versions = flexible_versions
            if len(flexible_versions) > 0:
                tagged_fields_field = FieldSpec(
                    'tagged_fields', Primitive('tagged_fields', None), flexible_versions, [])
                type.fields.append(tagged_fields_field)

    def compute_declaration_chain(self):
        """
    Computes all dependencies, what means all non-primitive types used by this type.
    They need to be declared before this struct is declared.
    """
        result = []
        for field in self.fields:
            field_dependencies = field.type.compute_declaration_chain()
            for field_dependency in field_dependencies:
                if field_dependency not in result:
                    result.append(field_dependency)
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
                    entry['full_declaration'] = '%s(%s): %s {};' % (
                        self.name, signature, field_list.constructor_init_list())
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
            field_list = FieldList(version, version in self.flexible_versions, self.fields)
            field_lists.append(field_list)
        return field_lists

    def compute_serialization_specs(self):
        result = []
        for field in self.fields:
            if field.type.has_flexible_handling():
                flexible = [x for x in field.version_usage if x in self.flexible_versions]
                non_flexible = [x for x in field.version_usage if x not in flexible]
                if non_flexible:
                    result.append(
                        FieldSerializationSpec(field, non_flexible, 'computeSize', 'encode'))
                if flexible:
                    result.append(
                        FieldSerializationSpec(
                            field, flexible, 'computeCompactSize', 'encodeCompact'))
            else:
                result.append(
                    FieldSerializationSpec(field, field.version_usage, 'computeSize', 'encode'))
        return result

    def deserializer_name_in_version(self, version, compact):
        return '%sV%dDeserializer' % (self.name, version)

    def name_in_c_case(self):
        import re
        s1 = re.sub('(.)([A-Z][a-z]+)', r'\1_\2', self.name)
        return re.sub('([a-z0-9])([A-Z])', r'\1_\2', s1).lower()

    def default_value(self):
        raise NotImplementedError('unable to create default value of complex type')

    def has_flexible_handling(self):
        return False

    def example_value_for_test(self, version):
        field_list = next(fl for fl in self.compute_field_lists() if fl.version == version)
        example_values = map(lambda x: x.example_value_for_test(version), field_list.used_fields())
        return '%s(%s)' % (self.name, ', '.join(example_values))

    def is_printable(self):
        return True


class RenderingHelper:
    """
    Utility function that allows us to process names in jinja easier.
    """

    @staticmethod
    def camel_case_to_snake_case(str):
        import re
        return re.sub('(?!^)([A-Z]+)', r'_\1', str)

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
            loader=jinja2.FileSystemLoader(
                searchpath=os.path.dirname(os.path.abspath(sys.argv[0]))))
        env.filters['camel_case_to_snake_case'] = RenderingHelper.camel_case_to_snake_case
        return env.get_template(template)
