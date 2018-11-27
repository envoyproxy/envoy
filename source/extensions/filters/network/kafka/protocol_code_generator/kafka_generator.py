#!/usr/bin/python

# usage:
# kafka_generator.py COMMAND OUTPUT FILES INPUT_FILES
# where:
# COMMAND : 'generate-source', to generate source files
#           'generate-test', to generate test files
# OUTPUT_FILES : if generate-source: location of 'requests.h' and 'kafka_request_resolver.cc',
#                if generate-test: location of 'requests_test.cc'
# INPUT_FILES: Kafka protocol json files to be processed

def main():
	import sys
	import os

	command = sys.argv[1]
	if 'generate-source' == command:
		requests_h_file = os.path.abspath(sys.argv[2])
		kafka_request_resolver_cc_file = os.path.abspath(sys.argv[3])
		input_files = sys.argv[4:]
	elif 'generate-test' == command:
		requests_test_cc_file = os.path.abspath(sys.argv[2])
		input_files = sys.argv[3:]
	else:
		raise ValueError('invalid command: ' + command)

	import re
	import json

	requests = []

	for input_file in input_files:
		with open(input_file, 'r') as fd:
			raw_contents = fd.read()
			without_comments = re.sub(r'//.*\n', '', raw_contents)
			request_spec = json.loads(without_comments)
			request = parse_request(request_spec)
			if request is not None: # debugging
				requests.append(request)

	requests.sort(key = lambda x: x.get_extra('api_key'))


	if 'generate-source' == command:
		complex_type_template = RenderingHelper.get_template('complex_type_template.j2')
		request_parsers_template = RenderingHelper.get_template('request_parser.j2')
		requests_h_contents = ''

		for request in requests:
			# structures holding payload data
			for dependency in request.declaration_chain:
				requests_h_contents += complex_type_template.render(complex_type = dependency)
			# request parser
			requests_h_contents += request_parsers_template.render(complex_type = request)

		# full file with headers, namespace declaration etc.
		requests_header_template = RenderingHelper.get_template('requests_h.j2')
		contents = requests_header_template.render(contents = requests_h_contents)

		with open(requests_h_file, 'w') as fd:
			fd.write(contents)

		kafka_request_resolver_template = RenderingHelper.get_template('kafka_request_resolver_cc.j2')
		contents = kafka_request_resolver_template.render(request_types = requests)

		with open(kafka_request_resolver_cc_file, 'w') as fd:
			fd.write(contents)

	if 'generate-test' == command:
		requests_test_template = RenderingHelper.get_template('requests_test_cc.j2')
		contents = requests_test_template.render(request_types = requests)

		with open(requests_test_cc_file, 'w') as fd:
			fd.write(contents)

def parse_request(spec):
	# a request is just a complex type, that has name & versions kept in differently named fields
	request_type_name = spec['name']
	request_versions = Statics.parse_version_string(spec['validVersions'], 2 << 16 - 1)
	return parse_complex_type(request_type_name, spec, request_versions).with_extra('api_key', spec['apiKey'])

def parse_complex_type(type_name, field_spec, versions):
	fields = []
	for child_field in field_spec['fields']:
		child = parse_field(child_field, versions[-1])
		fields.append(child)
	return Complex(type_name, fields, versions)

def parse_field(field_spec, highest_possible_version):
	# obviously, field cannot be used in version higher than its type's usage
	version_usage = Statics.parse_version_string(field_spec['versions'], highest_possible_version)
	version_usage_as_nullable = Statics.parse_version_string(field_spec['nullableVersions'], highest_possible_version) if 'nullableVersions' in field_spec else range(-1)
	parsed_type = parse_type(field_spec['type'], field_spec, highest_possible_version)
	return FieldSpec(field_spec['name'], parsed_type, version_usage, version_usage_as_nullable)

def parse_type(type_name, field_spec, highest_possible_version):
	# array types are defined as `[]underlying_type` instead of having its own element with type inside :\
	if (type_name.startswith('[]')):
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

	def __init__(self, version, fields):
		self.version = version
		self.fields = fields

	def used_fields(self):
		return filter(lambda x: x.used_in_version(self.version), self.fields)

	def constructor_signature(self):
		parameter_spec = map(lambda x: x.parameter_declaration(self.version), self.used_fields())
		return ', '.join(parameter_spec)

	def constructor_init_list(self):
		init_list = []
		for field in self.fields:
			if field.used_in_version(self.version):
				if field.is_nullable():
					if field.is_nullable_in_version(self.version):
						# field is optional<T>, and the parameter is optional<T> in this version
						init_list_item = '%s_{%s}' % (field.name, field.name)
						init_list.append(init_list_item)
					else:
						# field is optional<T>, and the parameter is T in this version
						init_list_item = '%s_{absl::make_optional(%s)}' % (field.name, field.name)
						init_list.append(init_list_item)
				else:
					# field is T, so parameter cannot be optional<T>
					init_list_item = '%s_{%s}' % (field.name, field.name)
					init_list.append(init_list_item)
			else:
				# field is not used in this version, so we need to put in default value
				init_list_item = '%s_{%s}' % (field.name, field.default_value())
				init_list.append(init_list_item)
			pass
		return ', '.join(init_list)

	def field_count(self):
		return len(self.used_fields())

	def example_value(self):
		return ', '.join(map(lambda x: x.example_value_for_test(self.version), self.used_fields()))

class FieldSpec:

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
			return 'absl::make_optional<%s>(%s)' % (self.type.name, self.type.example_value_for_test(version))
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
		raise NotImplementedError()

	def default_value(self):
		raise NotImplementedError()

	def example_value_for_test(self, version):
		raise NotImplementedError()

	def is_printable(self):
		raise NotImplementedError()

class Array(TypeSpecification):
	
	def __init__(self, underlying):
		self.underlying = underlying
		self.declaration_chain = self.underlying.declaration_chain

	@property
	def name(self):
		return 'std::vector<%s>' % self.underlying.name

	def deserializer_name_in_version(self, version):
		return 'ArrayDeserializer<%s, %s>' % (self.underlying.name, self.underlying.deserializer_name_in_version(version) )

	def default_value(self):
		return '{}'

	def example_value_for_test(self, version):
		return 'std::vector<%s>{ %s }' % (self.underlying.name, self.underlying.example_value_for_test(version))

	def is_printable(self):
		return self.underlying.is_printable()

class Primitive(TypeSpecification):

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

	# https://github.com/apache/kafka/tree/trunk/clients/src/main/resources/common/message#deserializing-messages
	KAFKA_TYPE_TO_DEFAULT_VALUE = {
		'string': '""',
		'bool': 'false',
		'int8': '0',
		'int16': '0',
		'int32': '0',
		'int64': '0',
		'bytes': '{}',
	}

	# to make test code more readable
	KAFKA_TYPE_TO_EXAMPLE_VALUE_FOR_TEST = {
		'string': '"string"',
		'bool': 'false',
		'int8': '8',
		'int16': '16',
		'int32': '32',
		'int64': '64ll',
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

	def __init__(self, name, fields, versions):
		self.name = name
		self.fields = fields
		self.versions = versions
		self.declaration_chain = self.__compute_declaration_chain()
		self.attributes = {}

	def __compute_declaration_chain(self):
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
		# field lists for different versions may not differ (as Kafka can bump version without any changes)
		# but constructors need to be unique
		signature_to_constructor = {}
		for field_list in self.compute_field_lists():
			signature = field_list.constructor_signature()
			constructor = signature_to_constructor.get(signature)
			if constructor is None:
				entry = {}
				entry['versions'] = [ field_list.version ]
				entry['signature'] = signature
				if (len(signature) > 0):
					entry['full_declaration'] = '%s(%s): %s {};' % (self.name, signature, field_list.constructor_init_list())
				else:
					entry['full_declaration'] = '%s() {};' % self.name
				signature_to_constructor[signature] = entry
			else:
				constructor['versions'].append(field_list.version)
		return sorted(signature_to_constructor.values(), key = lambda x: x['versions'][0])

	def compute_field_lists(self):
		field_lists = []
		for version in self.versions:
			field_list = FieldList(version, self.fields)
			field_lists.append(field_list)
		return field_lists;

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

	@staticmethod
	def get_template(template):
		import jinja2
		import os
		env = jinja2.Environment(loader = jinja2.FileSystemLoader(searchpath = os.path.dirname(os.path.abspath(__file__))))
		return env.get_template(template)

if __name__ == "__main__":
	main()
