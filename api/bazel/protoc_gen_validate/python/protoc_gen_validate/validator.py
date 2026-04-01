import ast
import re
import struct
import sys
import time
import uuid
from functools import lru_cache
from ipaddress import IPv4Address, IPv6Address, ip_address
from typing import Any
from urllib import parse as urlparse

from google.protobuf.message import Message
from jinja2 import Template
from validate_email import validate_email

if sys.version_info > (3, 9):
    unparse = ast.unparse
else:
    import astunparse
    unparse = astunparse.unparse

printer = ""

# Well known regex mapping.
regex_map = {
    "UNKNOWN": "",
    "HTTP_HEADER_NAME": r'^:?[0-9a-zA-Z!#$%&\'*+-.^_|~\x60]+$',
    "HTTP_HEADER_VALUE": r'^[^\u0000-\u0008\u000A-\u001F\u007F]*$',
    "HEADER_STRING": r'^[^\u0000\u000A\u000D]*$'
}


class ValidationFailed(Exception):
    pass


class ValidatingMessage(object):
    """Wrap a proto message to cache validate functions with the message class name.

    A validate function is defined per message class in protoc-gen-validate,
     so we can reuse an already generated function for the same message class.
    """

    def __init__(self, proto_message):
        self.DESCRIPTOR = proto_message.DESCRIPTOR

    def __hash__(self):
        return hash(self.DESCRIPTOR.full_name)

    def __eq__(self, other):
        if isinstance(other, ValidatingMessage):
            return self.DESCRIPTOR.full_name == other.DESCRIPTOR.full_name
        else:
            return False


def validate(proto_message: Message):
    return _validate_inner(ValidatingMessage(proto_message))(proto_message)


# Cache generated functions with the message descriptor's full_name as the cache key
@lru_cache()
def _validate_inner(proto_message: Message):
    func = file_template(proto_message)
    global printer
    printer += func + "\n"
    local_vars: dict[str, Any] = {}
    exec(func, globals(), local_vars)
    return local_vars['generate_validate']


class ChangeFuncName(ast.NodeTransformer):
    def visit_FunctionDef(self, node: ast.FunctionDef):
        node.name = node.name + "_all"  # add a suffix to the function name
        return node


class InitErr(ast.NodeTransformer):
    def visit_FunctionDef(self, node: ast.FunctionDef):
        node.body.insert(0, ast.parse("err = []").body[0])
        return node


class ReturnErr(ast.NodeTransformer):
    def visit_Return(self, node: ast.Return):
        # Change the return value of the function from None to err
        if hasattr(node.value, "value") and getattr(node.value, "value") is None:
            return ast.parse("return err").body[0]
        return node


class ChangeInnerCall(ast.NodeTransformer):
    def visit_Call(self, node: ast.Call):
        """Changed the validation function of nested messages from `validate` to
        `_validate_all`"""
        if isinstance(node.func, ast.Name) and node.func.id == "validate":
            node.func.id = "_validate_all"
        return node


class ChangeRaise(ast.NodeTransformer):
    def visit_Raise(self, node: ast.Raise):
        """
        before:
            raise ValidationFailed(reason)
        after:
            err.append(reason)
        """
        # According to the content in the template, the exception object of all `raise`
        # statements is `ValidationFailed`.
        if not isinstance(node.exc, ast.Call):
            return node
        return ast.Expr(
            value=ast.Call(
                args=node.exc.args,
                keywords=node.exc.keywords,
                func=ast.Attribute(
                    attr="append", ctx=ast.Load(), value=ast.Name(id="err", ctx=ast.Load())
                ),
            )
        )


class ChangeEmbedded(ast.NodeTransformer):
    """For embedded messages, there is a special structure in the template as follows:

    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
        embedded = validate(p.{{ name }})
        if embedded is not None:
            return embedded

    We need to convert this code into the following form:

    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
        err += _validate_all(p.{{ name }}

    """
    @staticmethod
    def _is_embedded_node(node: ast.Assign):
        """Check if substructures match

        pattern:
        embedded = validate(p.{{ name }})
        """
        if not isinstance(node, ast.Assign):
            return False
        if len(node.targets) != 1:
            return False
        target = node.targets[0]
        value = node.value
        if not (isinstance(target, ast.Name) and isinstance(value, ast.Call)):
            return False
        if not target.id == "embedded":
            return False
        return True

    def visit_If(self, node: ast.If):
        self.generic_visit(node)
        for child in ast.iter_child_nodes(node):
            if isinstance(child, ast.Assign) and self._is_embedded_node(child):
                new_node = ast.AugAssign(
                    target=ast.Name(id="err", ctx=ast.Store()), op=ast.Add(), value=child.value
                )  # err += _validate_all(p.{{ name }}
                node.body = [new_node]
                return node
        return node


class ChangeExpr(ast.NodeTransformer):

    """If there is a pure `_validate_all` function call in the template function,
    its return value needs to be recorded in err

    before:
    _validate_all(item)

    after:
    err += _validate_all(item}

    """

    def visit_Expr(self, node: ast.Expr):
        if not isinstance(node.value, ast.Call):
            return node
        call_node = node.value
        if not isinstance(call_node.func, ast.Name):
            return node
        if not call_node.func.id == "_validate_all":
            return node
        return ast.AugAssign(
            target=ast.Name(id="err", ctx=ast.Store()), op=ast.Add(), value=call_node
        )  # err += _validate_all(item}


# Cache generated functions with the message descriptor's full_name as the cache key
@lru_cache()
def _validate_all_inner(proto_message: Message):
    func = file_template(ValidatingMessage(proto_message))
    comment = func.split("\n")[1]
    func_ast = ast.parse(rf"{func}")
    for transformer in [
        ChangeFuncName,
        InitErr,
        ReturnErr,
        ChangeInnerCall,
        ChangeRaise,
        ChangeEmbedded,
        ChangeExpr,
    ]:  # order is important!
        func_ast = ast.fix_missing_locations(transformer().visit(func_ast))
    func_ast = ast.fix_missing_locations(func_ast)
    func = unparse(func_ast)
    func = comment + " All" + "\n" + func
    global printer
    printer += func + "\n"
    local_vars: dict[str, Any] = {}
    exec(func, globals(), local_vars)
    return local_vars['generate_validate_all']


def _validate_all(proto_message: Message) -> str:
    return _validate_all_inner(ValidatingMessage(proto_message))(proto_message)


# raise ValidationFailed if err
def validate_all(proto_message: Message):
    err = _validate_all(proto_message)
    if err:
        raise ValidationFailed('\n'.join(err))


def print_validate():
    return "".join([s for s in printer.splitlines(True) if s.strip()])


def has_validate(field):
    if field.GetOptions() is None:
        return False
    for option_descriptor, option_value in field.GetOptions().ListFields():
        if option_descriptor.full_name == "validate.rules":
            return True
    return False


def byte_len(s):
    try:
        return len(s.encode('utf-8'))
    except:  # noqa
        return len(s)


def _validateHostName(host):
    if not host:
        return False
    if len(host) > 253:
        return False

    if host[-1] == '.':
        host = host[:-1]

    for part in host.split("."):
        if len(part) == 0 or len(part) > 63:
            return False

        # Host names cannot begin or end with hyphens
        if part[0] == "-" or part[-1] == '-':
            return False
        for r in part:
            if (r < 'A' or r > 'Z') and (r < 'a' or r > 'z') and (r < '0' or r > '9') and r != '-':
                return False
    return True


def _validateEmail(addr):
    if '<' in addr and '>' in addr:
        addr = addr.split("<")[1].split(">")[0]

    if not validate_email(addr):
        return False

    if len(addr) > 254:
        return False

    parts = addr.split("@")
    if len(parts[0]) > 64:
        return False
    return _validateHostName(parts[1])


def _has_field(message_pb, property_name):
    # NOTE: As of proto3, HasField() only works for message fields, not for
    #       singular (non-message) fields. First try to use HasField and
    #       if it fails (with a ValueError) we manually consult the fields.
    try:
        return message_pb.HasField(property_name)
    except:  # noqa
        all_fields = set([field.name for field in message_pb.DESCRIPTOR.fields])
        return property_name in all_fields


def const_template(option_value, name):
    const_tmpl = """{%- if str(o.string) and o.string.HasField('const') -%}
    if {{ name }} != \"{{ o.string['const'] }}\":
        raise ValidationFailed(\"{{ name }} not equal to {{ o.string['const'] }}\")
    {%- elif str(o.bool) and o.bool['const'] != "" -%}
    if {{ name }} != {{ o.bool['const'] }}:
        raise ValidationFailed(\"{{ name }} not equal to {{ o.bool['const'] }}\")
    {%- elif str(o.bytes) and o.bytes.HasField('const') -%}
        {% if sys.version_info[0] >= 3 %}
    if {{ name }} != {{ o.bytes['const'] }}:
        raise ValidationFailed(\"{{ name }} not equal to {{ o.bytes['const'] }}\")
        {% else %}
    if {{ name }} != b\"{{ o.bytes['const'].encode('string_escape') }}\":
        raise ValidationFailed(\"{{ name }} not equal to {{ o.bytes['const'].encode('string_escape') }}\")
        {% endif %}
    {%- endif -%}
    """
    return Template(const_tmpl).render(sys=sys, o=option_value, name=name, str=str)


def in_template(value, name):
    in_tmpl = """
    {%- if value['in'] %}
    if {{ name }} not in {{ value['in'] }}:
        raise ValidationFailed(\"{{ name }} not in {{ value['in'] }}\")
    {%- endif -%}
    {%- if value['not_in'] %}
    if {{ name }} in {{ value['not_in'] }}:
        raise ValidationFailed(\"{{ name }} in {{ value['not_in'] }}\")
    {%- endif -%}
    """
    return Template(in_tmpl).render(value=value, name=name)


def string_template(option_value, name):
    if option_value.string.well_known_regex:
        known_regex_type = option_value.string.DESCRIPTOR.fields_by_name['well_known_regex'].enum_type
        regex_value = option_value.string.well_known_regex
        regex_name = known_regex_type.values_by_number[regex_value].name
        if regex_name in ["HTTP_HEADER_NAME", "HTTP_HEADER_VALUE"] and not option_value.string.strict:
            option_value.string.pattern = regex_map["HEADER_STRING"]
        else:
            option_value.string.pattern = regex_map[regex_name]
    str_templ = """
    {%- set s = o.string -%}
    {% set i = 0 %}
    {%- if s['ignore_empty'] %}
    if {{ name }}:
    {% set i = 4 %}
    {%- endif -%}
    {% filter indent(i,True) %}
    {{ const_template(o, name) -}}
    {{ in_template(o.string, name) -}}
    {%- if s['len'] %}
    if len({{ name }}) != {{ s['len'] }}:
        raise ValidationFailed(\"{{ name }} length does not equal {{ s['len'] }}\")
    {%- endif -%}
    {%- if s['min_len'] %}
    if len({{ name }}) < {{ s['min_len'] }}:
        raise ValidationFailed(\"{{ name }} length is less than {{ s['min_len'] }}\")
    {%- endif -%}
    {%- if s['max_len'] %}
    if len({{ name }}) > {{ s['max_len'] }}:
        raise ValidationFailed(\"{{ name }} length is more than {{ s['max_len'] }}\")
    {%- endif -%}
    {%- if s['len_bytes'] %}
    if byte_len({{ name }}) != {{ s['len_bytes'] }}:
        raise ValidationFailed(\"{{ name }} length does not equal {{ s['len_bytes'] }}\")
    {%- endif -%}
    {%- if s['min_bytes'] %}
    if byte_len({{ name }}) < {{ s['min_bytes'] }}:
        raise ValidationFailed(\"{{ name }} length is less than {{ s['min_bytes'] }}\")
    {%- endif -%}
    {%- if s['max_bytes'] %}
    if byte_len({{ name }}) > {{ s['max_bytes'] }}:
        raise ValidationFailed(\"{{ name }} length is greater than {{ s['max_bytes'] }}\")
    {%- endif -%}
    {%- if s['pattern'] %}
    if re.search(r\'{{ s['pattern'] }}\', {{ name }}) is None:
        raise ValidationFailed(\"{{ name }} pattern does not match {{ s['pattern'] }}\")
    {%- endif -%}
    {%- if s['prefix'] %}
    if not {{ name }}.startswith(\"{{ s['prefix'] }}\"):
        raise ValidationFailed(\"{{ name }} does not start with prefix {{ s['prefix'] }}\")
    {%- endif -%}
    {%- if s['suffix'] %}
    if not {{ name }}.endswith(\"{{ s['suffix'] }}\"):
        raise ValidationFailed(\"{{ name }} does not end with suffix {{ s['suffix'] }}\")
    {%- endif -%}
    {%- if s['contains'] %}
    if not \"{{ s['contains'] }}\" in {{ name }}:
        raise ValidationFailed(\"{{ name }} does not contain {{ s['contains'] }}\")
    {%- endif -%}
    {%- if s['not_contains'] %}
    if \"{{ s['not_contains'] }}\" in {{ name }}:
        raise ValidationFailed(\"{{ name }} contains {{ s['not_contains'] }}\")
    {%- endif -%}
    {%- if s['email'] %}
    if not _validateEmail({{ name }}):
        raise ValidationFailed(\"{{ name }} is not a valid email\")
    {%- endif -%}
    {%- if s['hostname'] %}
    if not _validateHostName({{ name }}):
        raise ValidationFailed(\"{{ name }} is not a valid email\")
    {%- endif -%}
    {%- if s['address'] %}
    try:
        ip_address({{ name }})
    except ValueError:
        if not _validateHostName({{ name }}):
            raise ValidationFailed(\"{{ name }} is not a valid address\")
    {%- endif -%}
    {%- if s['ip'] %}
    try:
        ip_address({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid ip\")
    {%- endif -%}
    {%- if s['ipv4'] %}
    try:
        IPv4Address({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid ipv4\")
    {%- endif -%}
    {%- if s['ipv6'] %}
    try:
        IPv6Address({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid ipv6\")
    {%- endif %}
    {%- if s['uri'] %}
    url = urlparse.urlparse({{ name }})
    if not all([url.scheme, url.netloc, url.path]):
        raise ValidationFailed(\"{{ name }} is not a valid uri\")
    {%- endif %}
    {%- if s['uri_ref'] %}
    url = urlparse.urlparse({{ name }})
    if not all([url.scheme, url.path]) and url.fragment:
        raise ValidationFailed(\"{{ name }} is not a valid uri ref\")
    {%- endif -%}
    {%- if s['uuid'] %}
    try:
        uuid.UUID({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid UUID\")
    {%- endif -%}
    {% endfilter %}
    """
    return Template(str_templ).render(o=option_value, name=name, const_template=const_template, in_template=in_template)


def required_template(value, name):
    req_tmpl = """{%- if value['required'] %}
    if not _has_field(p, \"{{ name.split('.')[-1] }}\"):
        raise ValidationFailed(\"{{ name }} is required.\")
    {%- endif -%}
    """
    return Template(req_tmpl).render(value=value, name=name)


def message_template(option_value, name, repeated=False):
    message_tmpl = """{%- if m.message %}
    {{- required_template(m.message, name) }}
    {%- endif -%}
    {%- if m.message and m.message['skip'] %}
    # Skipping validation for {{ name }}
    {%- else %}
    {% if repeated %}
    if {{ name }}:
    {% else %}
    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
    {% endif %}
        embedded = validate(p.{{ name }})
        if embedded is not None:
            return embedded
    {%- endif -%}
    """
    return Template(message_tmpl).render(
        m=option_value, name=name, required_template=required_template, repeated=repeated)


def bool_template(option_value, name):
    bool_tmpl = """
    {{ const_template(o, name) -}}
    """
    return Template(bool_tmpl).render(o=option_value, name=name, const_template=const_template)


def num_template(option_value, name, num):
    num_tmpl = """
    {% set i = 0 %}
    {%- if num.HasField('ignore_empty') %}
    if {{ name }}:
    {% set i = 4 %}
    {%- endif -%}
    {% filter indent(i,True) %}
    {%- if num.HasField('const') and str(o.float) == "" -%}
    if {{ name }} != {{ num['const'] }}:
        raise ValidationFailed(\"{{ name }} not equal to {{ num['const'] }}\")
    {%- endif -%}
    {%- if num.HasField('const') and str(o.float) != "" %}
    if {{ name }} != struct.unpack(\"f\", struct.pack(\"f\", ({{ num['const'] }})))[0]:
        raise ValidationFailed(\"{{ name }} not equal to {{ num['const'] }}\")
    {%- endif -%}
    {{ in_template(num, name) }}
    {%- if num.HasField('lt') %}
        {%- if num.HasField('gt') %}
            {%- if num['lt'] > num['gt'] %}
    if {{ name }} <= {{ num['gt'] }} or {{ name }} >= {{ num ['lt'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['lt'], num['gt'] }}\")
            {%- else %}
    if {{ name }} >= {{ num['lt'] }} and {{ name }} <= {{ num['gt'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['gt'], num['lt'] }}\")
            {%- endif -%}
        {%- elif num.HasField('gte') %}
            {%- if num['lt'] > num['gte'] %}
    if {{ name }} < {{ num['gte'] }} or {{ name }} >= {{ num ['lt'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['lt'], num['gte'] }}\")
            {%- else %}
    if {{ name }} >= {{ num['lt'] }} and {{ name }} < {{ num['gte'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['gte'], num['lt'] }}\")
            {%- endif -%}
        {%- else %}
    if {{ name }} >= {{ num['lt'] }}:
        raise ValidationFailed(\"{{ name }} is not lesser than {{ num['lt'] }}\")
        {%- endif -%}
    {%- elif num.HasField('lte') %}
        {%- if num.HasField('gt') %}
            {%- if num['lte'] > num['gt'] %}
    if {{ name }} <= {{ num['gt'] }} or {{ name }} > {{ num ['lte'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['lte'], num['gt'] }}\")
            {%- else %}
    if {{ name }} > {{ num['lte'] }} and {{ name }} <= {{ num['gt'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['gt'], num['lte'] }}\")
            {%- endif -%}
        {%- elif num.HasField('gte') %}
            {%- if num['lte'] > num['gte'] %}
    if {{ name }} < {{ num['gte'] }} or {{ name }} > {{ num ['lte'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['lte'], num['gte'] }}\")
            {%- else %}
    if {{ name }} > {{ num['lte'] }} and {{ name }} < {{ num['gte'] }}:
        raise ValidationFailed(\"{{ name }} is not in range {{ num['gte'], num['lte'] }}\")
            {%- endif -%}
        {%- else %}
    if {{ name }} > {{ num['lte'] }}:
        raise ValidationFailed(\"{{ name }} is not lesser than or equal to {{ num['lte'] }}\")
        {%- endif -%}
    {%- elif num.HasField('gt') %}
    if {{ name }} <= {{ num['gt'] }}:
        raise ValidationFailed(\"{{ name }} is not greater than {{ num['gt'] }}\")
    {%- elif num.HasField('gte') %}
    if {{ name }} < {{ num['gte'] }}:
        raise ValidationFailed(\"{{ name }} is not greater than or equal to {{ num['gte'] }}\")
    {%- endif -%}
    {% endfilter %}
    """
    return Template(num_tmpl).render(o=option_value, name=name, num=num, in_template=in_template, str=str)


def dur_arr(dur):
    value = 0
    arr = []
    for val in dur:
        value += val.seconds
        value += (10**-9 * val.nanos)
        arr.append(value)
        value = 0
    return arr


def dur_lit(dur):
    value = dur.seconds + (10**-9 * dur.nanos)
    return value


def duration_template(option_value, name, repeated=False):
    dur_tmpl = """
    {{- required_template(o.duration, name) }}
    {% if repeated %}
    if {{ name }}:
    {% else %}
    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
    {% endif %}
        dur = {{ name }}.seconds + round((10**-9 * {{ name }}.nanos), 9)
        {%- set dur = o.duration -%}
        {%- if dur.HasField('lt') %}
        lt = {{ dur_lit(dur['lt']) }}
        {% endif %}
        {%- if dur.HasField('lte') %}
        lte = {{ dur_lit(dur['lte']) }}
        {% endif %}
        {%- if dur.HasField('gt') %}
        gt = {{ dur_lit(dur['gt']) }}
        {% endif %}
        {%- if dur.HasField('gte') %}
        gte = {{ dur_lit(dur['gte']) }}
        {% endif %}
        {%- if dur.HasField('const') %}
        if dur != {{ dur_lit(dur['const']) }}:
            raise ValidationFailed(\"{{ name }} is not equal to {{ dur_lit(dur['const']) }}\")
        {%- endif -%}
        {%- if dur['in'] %}
        if dur not in {{ dur_arr(dur['in']) }}:
            raise ValidationFailed(\"{{ name }} is not in {{ dur_arr(dur['in']) }}\")
        {%- endif -%}
        {%- if dur['not_in'] %}
        if dur in {{ dur_arr(dur['not_in']) }}:
            raise ValidationFailed(\"{{ name }} is not in {{ dur_arr(dur['not_in']) }}\")
        {%- endif -%}
        {%- if dur.HasField('lt') %}
            {%- if dur.HasField('gt') %}
                {%- if dur_lit(dur['lt']) > dur_lit(dur['gt']) %}
        if dur <= gt or dur >= lt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['lt']), dur_lit(dur['gt']) }}\")
                {%- else -%}
        if dur >= lt and dur <= gt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['gt']), dur_lit(dur['lt']) }}\")
                {%- endif -%}
            {%- elif dur.HasField('gte') %}
                {%- if dur_lit(dur['lt']) > dur_lit(dur['gte']) %}
        if dur < gte or dur >= lt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['lt']), dur_lit(dur['gte']) }}\")
                {%- else -%}
        if dur >= lt and dur < gte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['gte']), dur_lit(dur['lt']) }}\")
                {%- endif -%}
            {%- else -%}
        if dur >= lt:
            raise ValidationFailed(\"{{ name }} is not lesser than {{ dur_lit(dur['lt']) }}\")
            {%- endif -%}
        {%- elif dur.HasField('lte') %}
            {%- if dur.HasField('gt') %}
                {%- if dur_lit(dur['lte']) > dur_lit(dur['gt']) %}
        if dur <= gt or dur > lte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['lte']), dur_lit(dur['gt']) }}\")
                {%- else -%}
        if dur > lte and dur <= gt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['gt']), dur_lit(dur['lte']) }}\")
                {%- endif -%}
            {%- elif dur.HasField('gte') %}
                {%- if dur_lit(dur['lte']) > dur_lit(dur['gte']) %}
        if dur < gte or dur > lte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['lte']), dur_lit(dur['gte']) }}\")
                {%- else -%}
        if dur > lte and dur < gte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(dur['gte']), dur_lit(dur['lte']) }}\")
                {%- endif -%}
            {%- else -%}
        if dur > lte:
            raise ValidationFailed(\"{{ name }} is not lesser than or equal to {{ dur_lit(dur['lte']) }}\")
            {%- endif -%}
        {%- elif dur.HasField('gt') %}
        if dur <= gt:
            raise ValidationFailed(\"{{ name }} is not greater than {{ dur_lit(dur['gt']) }}\")
        {%- elif dur.HasField('gte') %}
        if dur < gte:
            raise ValidationFailed(\"{{ name }} is not greater than or equal to {{ dur_lit(dur['gte']) }}\")
        {%- endif -%}
    """
    return Template(dur_tmpl).render(o=option_value, name=name, required_template=required_template,
                                     dur_lit=dur_lit, dur_arr=dur_arr, repeated=repeated)


def timestamp_template(option_value, name, repeated=False):
    timestamp_tmpl = """
    {{- required_template(o.timestamp, name) }}
    {% if repeated %}
    if {{ name }}:
    {% else %}
    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
    {% endif %}
        ts = {{ name }}.seconds + round((10**-9 * {{ name }}.nanos), 9)
        {%- set ts = o.timestamp -%}
        {%- if ts.HasField('lt') %}
        lt = {{ dur_lit(ts['lt']) }}
        {% endif -%}
        {%- if ts.HasField('lte') %}
        lte = {{ dur_lit(ts['lte']) }}
        {% endif -%}
        {%- if ts.HasField('gt') %}
        gt = {{ dur_lit(ts['gt']) }}
        {% endif -%}
        {%- if ts.HasField('gte') %}
        gte = {{ dur_lit(ts['gte']) }}
        {% endif -%}
        {%- if ts.HasField('const') %}
        if ts != {{ dur_lit(ts['const']) }}:
            raise ValidationFailed(\"{{ name }} is not equal to {{ dur_lit(ts['const']) }}\")
        {% endif %}
        {%- if ts['in'] %}
        if ts not in {{ dur_arr(ts['in']) }}:
            raise ValidationFailed(\"{{ name }} is not in {{ dur_arr(ts['in']) }}\")
        {%- endif %}
        {%- if ts['not_in'] %}
        if ts in {{ dur_arr(ts['not_in']) }}:
            raise ValidationFailed(\"{{ name }} is not in {{ dur_arr(ts['not_in']) }}\")
        {%- endif %}
        {%- if ts.HasField('lt') %}
            {%- if ts.HasField('gt') %}
                {%- if dur_lit(ts['lt']) > dur_lit(ts['gt']) %}
        if ts <= gt or ts >= lt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['lt']), dur_lit(ts['gt']) }}\")
                {%- else -%}
        if ts >= lt and ts <= gt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['gt']), dur_lit(ts['lt']) }}\")
                {%- endif -%}
            {%- elif ts.HasField('gte') %}
                {%- if dur_lit(ts['lt']) > dur_lit(ts['gte']) %}
        if ts < gte or ts >= lt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['lt']), dur_lit(ts['gte']) }}\")
                {%- else -%}
        if ts >= lt and ts < gte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['gte']), dur_lit(ts['lt']) }}\")
                {%- endif -%}
            {%- else -%}
        if ts >= lt:
            raise ValidationFailed(\"{{ name }} is not lesser than {{ dur_lit(ts['lt']) }}\")
            {%- endif -%}
        {%- elif ts.HasField('lte') %}
            {%- if ts.HasField('gt') %}
                {%- if dur_lit(ts['lte']) > dur_lit(ts['gt']) %}
        if ts <= gt or ts > lte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['lte']), dur_lit(ts['gt']) }}\")
                {%- else -%}
        if ts > lte and ts <= gt:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['gt']), dur_lit(ts['lte']) }}\")
                {%- endif -%}
            {%- elif ts.HasField('gte') %}
                {%- if dur_lit(ts['lte']) > dur_lit(ts['gte']) %}
        if ts < gte or ts > lte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['lte']), dur_lit(ts['gte']) }}\")
                {%- else -%}
        if ts > lte and ts < gte:
            raise ValidationFailed(\"{{ name }} is not in range {{ dur_lit(ts['gte']), dur_lit(ts['lte']) }}\")
                {%- endif -%}
            {%- else -%}
        if ts > lte:
            raise ValidationFailed(\"{{ name }} is not lesser than or equal to {{ dur_lit(ts['lte']) }}\")
            {%- endif -%}
        {%- elif ts.HasField('gt') %}
        if ts <= gt:
            raise ValidationFailed(\"{{ name }} is not greater than {{ dur_lit(ts['gt']) }}\")
        {%- elif ts.HasField('gte') %}
        if ts < gte:
            raise ValidationFailed(\"{{ name }} is not greater than or equal to {{ dur_lit(ts['gte']) }}\")
        {%- elif ts.HasField('lt_now') %}
        now = time.time()
            {%- if ts.HasField('within') %}
        within = {{ dur_lit(ts['within']) }}
        if ts >= now or ts <= now - within:
            raise ValidationFailed(\"{{ name }} is not within range {{ dur_lit(ts['within']) }}\")
            {%- else %}
        if ts >= now:
            raise ValidationFailed(\"{{ name }} is not lesser than now\")
            {%- endif -%}
        {%- elif ts.HasField('gt_now') %}
        now = time.time()
            {%- if ts.HasField('within') %}
        within = {{ dur_lit(ts['within']) }}
        if ts <= now or ts >= now + within:
            raise ValidationFailed(\"{{ name }} is not within range {{ dur_lit(ts['within']) }}\")
            {%- else %}
        if ts <= now:
            raise ValidationFailed(\"{{ name }} is not greater than now\")
            {%- endif -%}
        {%- elif ts.HasField('within') %}
        now = time.time()
        within = {{ dur_lit(ts['within']) }}
        if ts >= now + within or ts <= now - within:
             raise ValidationFailed(\"{{ name }} is not within range {{ dur_lit(ts['within']) }}\")
        {%- endif -%}
    """
    return Template(timestamp_tmpl).render(o=option_value, name=name, required_template=required_template,
                                           dur_lit=dur_lit, dur_arr=dur_arr, repeated=repeated)


def wrapper_template(option_value, name, repeated=False):
    wrapper_tmpl = """
    {% if repeated %}
    if {{ name }}:
    {% else %}
    if p.HasField(\"{{ name[2:] }}\"):
    {% endif %}
        {%- if str(option_value.float) %}
        {{- num_template(option_value, name + ".value", option_value.float)|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.double) %}
        {{- num_template(option_value, name + ".value", option_value.double)|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.int32) %}
        {{- num_template(option_value, name + ".value", option_value.int32)|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.int64) %}
        {{- num_template(option_value, name + ".value", option_value.int64)|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.uint32) %}
        {{- num_template(option_value, name + ".value", option_value.uint32)|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.uint64) %}
        {{- num_template(option_value, name + ".value", option_value.uint64)|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.bool) %}
        {{- bool_template(option_value, name + ".value")|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.string) %}
        {{- string_template(option_value, name + ".value")|indent(4,True) -}}
        {% endif -%}
        {%- if str(option_value.bytes) %}
        {{- bytes_template(option_value, name + ".value")|indent(4,True) -}}
        {% endif -%}
    {%- if str(option_value.message) and option_value.message['required'] %}
    else:
        raise ValidationFailed(\"{{ name }} is required.\")
    {%- endif %}
    """
    return Template(wrapper_tmpl).render(option_value=option_value, name=name, str=str, num_template=num_template,
                                         bool_template=bool_template, string_template=string_template,
                                         bytes_template=bytes_template, repeated=repeated)


def enum_values(field):
    return [x.number for x in field.enum_type.values]


def enum_name(field, number):
    for x in field.enum_type.values:
        if x.number == number:
            return x.name
    return ""


def enum_names(field, numbers):
    m = {x.number: x.name for x in field.enum_type.values}
    return "[" + "".join([m[n] for n in numbers]) + "]"


def enum_const_template(value, name, field):
    const_tmpl = """{%- if str(value) and value['const'] -%}
    if {{ name }} != {{ value['const'] }}:
        raise ValidationFailed(\"{{ name }} not equal to {{ enum_name(field, value['const']) }}\")
    {%- endif -%}
    """
    return Template(const_tmpl).render(value=value, name=name, field=field, enum_name=enum_name, str=str)


def enum_in_template(value, name, field):
    in_tmpl = """
    {%- if value['in'] %}
    if {{ name }} not in {{ value['in'] }}:
        raise ValidationFailed(\"{{ name }} not in {{ enum_names(field, value['in']) }}\")
    {%- endif -%}
    {%- if value['not_in'] %}
    if {{ name }} in {{ value['not_in'] }}:
        raise ValidationFailed(\"{{ name }} in {{ enum_names(field, value['not_in']) }}\")
    {%- endif -%}
    """
    return Template(in_tmpl).render(value=value, name=name, field=field, enum_names=enum_names)


def enum_template(option_value, name, field):
    enum_tmpl = """
    {{ enum_const_template(option_value.enum, name, field) -}}
    {{ enum_in_template(option_value.enum, name, field) -}}
    {% if option_value.enum['defined_only'] %}
    if {{ name }} not in {{ enum_values(field) }}:
        raise ValidationFailed(\"{{ name }} is not defined\")
    {% endif %}
    """
    return Template(enum_tmpl).render(option_value=option_value, name=name, enum_const_template=enum_const_template,
                                      enum_in_template=enum_in_template, field=field, enum_values=enum_values)


def any_template(option_value, name, repeated=False):
    any_tmpl = """
    {{- required_template(o, name) }}
    {%- if o['in'] %}
    {% if repeated %}
    if {{ name }}:
    {% else %}
    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
    {% endif %}
        if {{ name }}.type_url not in {{ o['in'] }}:
            raise ValidationFailed(\"{{ name }} not in {{ o['in'] }}\")
    {%- endif %}
    {%- if o['not_in'] %}
    {% if repeated %}
    if {{ name }}:
    {% else %}
    if _has_field(p, \"{{ name.split('.')[-1] }}\"):
    {% endif %}
        if {{ name }}.type_url in {{ o['not_in'] }}:
            raise ValidationFailed(\"{{ name }} in {{ o['not_in'] }}\")
    {%- endif %}
    """
    return Template(any_tmpl).render(
        o=option_value.any, name=name, required_template=required_template, repeated=repeated)


def bytes_template(option_value, name):
    bytes_tmpl = """
    {% set i = 0 %}
    {%- if b['ignore_empty'] %}
    if {{ name }}:
    {% set i = 4 %}
    {%- endif -%}
    {% filter indent(i,True) %}
    {{ const_template(o, name) -}}
    {{ in_template(o.bytes, name) -}}
    {%- if b['len'] %}
    if len({{ name }}) != {{ b['len'] }}:
        raise ValidationFailed(\"{{ name }} length does not equal {{ b['len'] }}\")
    {%- endif -%}
    {%- if b['min_len'] %}
    if len({{ name }}) < {{ b['min_len'] }}:
        raise ValidationFailed(\"{{ name }} length is less than {{ b['min_len'] }}\")
    {%- endif -%}
    {%- if b['max_len'] %}
    if len({{ name }}) > {{ b['max_len'] }}:
        raise ValidationFailed(\"{{ name }} length is more than {{ b['max_len'] }}\")
    {%- endif -%}
    {%- if b['ip'] %}
    try:
        ip_address({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid ip\")
    {%- endif -%}
    {%- if b['ipv4'] %}
    try:
        IPv4Address({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid ipv4\")
    {%- endif -%}
    {%- if b['ipv6'] %}
    try:
        IPv6Address({{ name }})
    except ValueError:
        raise ValidationFailed(\"{{ name }} is not a valid ipv6\")
    {%- endif -%}
    {% if b['pattern'] %}
        {% if sys.version_info[0] >= 3%}
    if re.search({{ b['pattern'].encode('unicode-escape') }}, {{ name }}) is None:
        raise ValidationFailed(\"{{ name }} pattern does not match b['pattern'].encode('unicode-escape')\")
        {% else %}
    if re.search(b\"{{ b['pattern'].encode('unicode-escape') }}\", {{ name }}) is None:
        raise ValidationFailed(\"{{ name }} pattern does not match \")
        {% endif %}
    {% endif %}
    {% if b['contains'] %}
        {% if sys.version_info[0] >= 3 %}
    if not {{ b['contains'] }} in {{ name }}:
        raise ValidationFailed(\"{{ name }} does not contain {{ b['contains'] }}\")
        {% else %}
    if not b\"{{ b['contains'].encode('string_escape') }}\" in {{ name }}:
        raise ValidationFailed(\"{{ name }} does not contain \")
        {% endif %}
    {% endif %}
    {% if b['prefix'] %}
        {% if sys.version_info[0] >= 3 %}
    if not {{ name }}.startswith({{ b['prefix'] }}):
        raise ValidationFailed(\"{{ name }} does not start with prefix {{ b['prefix'] }}\")
        {% else %}
    if not {{name}}.startswith(b\"{{ b['prefix'].encode('string_escape') }}\"):
        raise ValidationFailed(\"{{ name }} does not start with prefix {{ b['prefix'].encode('string_escape') }}\")
        {% endif %}
    {% endif %}
    {% if b['suffix'] %}
        {% if sys.version_info[0] >= 3 %}
    if not {{ name }}.endswith({{ b['suffix'] }}):
        raise ValidationFailed(\"{{ name }} does not end with suffix {{ b['suffix'] }}\")
        {% else %}
    if not {{name}}.endswith(b\"{{ b['suffix'].encode('string_escape') }}\"):
        raise ValidationFailed(\"{{ name }} does not end with suffix {{ b['suffix'] }}\")
        {% endif %}
    {% endif %}
    {% endfilter %}
    """
    return Template(bytes_tmpl).render(sys=sys, o=option_value, name=name,
                                       const_template=const_template, in_template=in_template, b=option_value.bytes)


def switcher_template(accessor, name, field, map=False):
    switcher_tmpl = """
    {%- if str(accessor.float) %}
    {{- num_template(accessor, name, accessor.float)|indent(4,True) -}}
    {%- elif str(accessor.double) %}
    {{- num_template(accessor, name, accessor.double)|indent(4,True) -}}
    {%- elif str(accessor.int32) %}
    {{- num_template(accessor, name, accessor.int32)|indent(4,True) -}}
    {%- elif str(accessor.int64) %}
    {{- num_template(accessor, name, accessor.int64)|indent(4,True) -}}
    {%- elif str(accessor.uint32) %}
    {{- num_template(accessor, name, accessor.uint32)|indent(4,True) -}}
    {%- elif str(accessor.uint64) %}
    {{- num_template(accessor, name, accessor.uint64)|indent(4,True) -}}
    {%- elif str(accessor.sint32) %}
    {{- num_template(accessor, name, accessor.sint32)|indent(4,True) -}}
    {%- elif str(accessor.sint64) %}
    {{- num_template(accessor, name, accessor.sint64)|indent(4,True) -}}
    {%- elif str(accessor.fixed32) %}
    {{- num_template(accessor, name, accessor.fixed32)|indent(4,True) -}}
    {%- elif str(accessor.fixed64) %}
    {{- num_template(accessor, name, accessor.fixed64)|indent(4,True) -}}
    {%- elif str(accessor.sfixed32) %}
    {{- num_template(accessor, name, accessor.sfixed32)|indent(4,True) -}}
    {%- elif str(accessor.sfixed64) %}
    {{- num_template(accessor, name, accessor.sfixed64)|indent(4,True) -}}
    {%- elif str(accessor.bool) %}
    {{- bool_template(accessor, name)|indent(4,True) -}}
    {%- elif str(accessor.string) %}
    {{- string_template(accessor, name)|indent(4,True) -}}
    {%- elif str(accessor.enum) and map %}
    {{- enum_template(accessor, name, field.message_type.fields[1])|indent(4,True) -}}
    {%- elif str(accessor.enum) and not map %}
    {{- enum_template(accessor, name, field)|indent(4,True) -}}
    {%- elif str(accessor.duration) %}
    {{- duration_template(accessor, name, True)|indent(4,True) -}}
    {%- elif str(accessor.timestamp) %}
    {{- timestamp_template(accessor, name, True)|indent(4,True) -}}
    {%- elif str(accessor.message) %}
    {{- message_template(accessor, name, True)|indent(4,True) -}}
    {%- elif str(accessor.any) %}
    {{- any_template(accessor, name, True)|indent(4,True) -}}
    {%- elif str(accessor.message) %}
    {{- message_template(accessor, name, True)|indent(4,True) -}}
    {%- endif %}
    """
    return Template(switcher_tmpl).render(accessor=accessor, name=name, str=str, num_template=num_template,
                                          bool_template=bool_template, string_template=string_template,
                                          enum_template=enum_template, duration_template=duration_template,
                                          timestamp_template=timestamp_template, any_template=any_template,
                                          message_template=message_template, field=field, map=map)


def repeated_template(option_value, name, field):
    rep_tmpl = """
    {% set i = 0 %}
    {%- if o and o.repeated['ignore_empty'] %}
    if {{ name }}:
    {% set i = 4 %}
    {%- endif -%}
    {% filter indent(i,True) %}
    {%- if o and o.repeated['min_items'] %}
    if len({{ name }}) < {{ o.repeated['min_items'] }}:
        raise ValidationFailed(\"{{ name }} needs to contain at least {{ o.repeated['min_items'] }} items\")
    {%- endif %}
    {%- if o and o.repeated['max_items'] %}
    if len({{ name }}) > {{ o.repeated['max_items'] }}:
        raise ValidationFailed(\"{{ name }} needs to contain at most {{ o.repeated['max_items'] }} items\")
    {%- endif %}
    {%- if o and o.repeated['unique'] %}
    seen = set()
    for item in {{ name }}:
        if item in seen:
            raise ValidationFailed(\"{{ name }} must contain unique items. %s has been repeated.\" %item)
        else:
            seen.add(item)
    {%- endif %}
    {%- if message_type %}
    for item in {{ name }}:
        {%- if o and o.repeated and o.repeated.items.message.skip %}
        pass
        {% else %}
        validate(item)
        {% endif %}
    {%- endif %}
    {%- if o and str(o.repeated['items']) %}
    for item in {{ name }}:
        {%- set accessor = o.repeated['items'] -%}
        {{ switcher_template(accessor, 'item', field) }}
        pass
    {%- endif %}
    {% endfilter %}
    """
    return Template(rep_tmpl).render(o=option_value, name=name, message_type=field.message_type,
                                     str=str, field=field, switcher_template=switcher_template)


def is_map(field):
    return field.label == 3 and field.message_type and len(field.message_type.fields) == 2 and \
        field.message_type.fields[0].name == "key" and field.message_type.fields[1].name == "value"


def map_template(option_value, name, field):
    map_tmpl = """
    {% set i = 0 %}
    {%- if o and o.map['ignore_empty'] %}
    if {{ name }}:
    {% set i = 4 %}
    {%- endif -%}
    {% filter indent(i,True) %}
    {%- if o and o.map['min_pairs'] %}
    if len({{ name }}) < {{ o.map['min_pairs'] }}:
        raise ValidationFailed(\"{{ name }} needs to contain at least {{ o.map['min_pairs'] }} items\")
    {%- endif %}
    {%- if o and o.map['max_pairs'] %}
    if len({{ name }}) > {{ o.map['max_pairs'] }}:
        raise ValidationFailed(\"{{ name }} can contain at most {{ o.map['max_pairs'] }} items\")
    {%- endif %}
    {%- if o and (str(o.map['keys']) or str(o.map['values']))%}
    for key in {{ name }}:
        {%- set keys = o.map['keys'] -%}
        {%- set values = o.map['values'] -%}
        {%- if str(keys.double) %}
        {{- num_template(keys, 'key', keys.double)|indent(4,True) -}}
        {%- elif str(keys.int32) %}
        {{- num_template(keys, 'key', keys.int32)|indent(4,True) -}}
        {%- elif str(keys.int64) %}
        {{- num_template(keys, 'key', keys.int64)|indent(4,True) -}}
        {%- elif str(keys.uint32) %}
        {{- num_template(keys, 'key', keys.uint32)|indent(4,True) -}}
        {%- elif str(keys.uint64) %}
        {{- num_template(keys, 'key', keys.uint64)|indent(4,True) -}}
        {%- elif str(keys.sint32) %}
        {{- num_template(keys, 'key', keys.sint32)|indent(4,True) -}}
        {%- elif str(keys.sint64) %}
        {{- num_template(keys, 'key', keys.sint64)|indent(4,True) -}}
        {%- elif str(keys.fixed32) %}
        {{- num_template(keys, 'key', keys.fixed32)|indent(4,True) -}}
        {%- elif str(keys.fixed64) %}
        {{- num_template(keys, 'key', keys.fixed64)|indent(4,True) -}}
        {%- elif str(keys.sfixed32) %}
        {{- num_template(keys, 'key', keys.sfixed32)|indent(4,True) -}}
        {%- elif str(keys.sfixed64) %}
        {{- num_template(keys, 'key', keys.sfixed64)|indent(4,True) -}}
        {%- elif str(keys.bool) %}
        {{- bool_template(keys, 'key')|indent(4,True) -}}
        {%- elif str(keys.string) %}
        {{- string_template(keys, 'key')|indent(4,True) -}}
        {%- endif %}
        {%- set values = o.map['values'] -%}
        {{ switcher_template(values, name +'[key]', field, True) }}
        pass
    {%- elif field.message_type.fields[1].message_type %}
    for key in {{ name }}:
        validate({{ name }}[key])
    {%- endif %}
    {% endfilter %}
    """
    return Template(map_tmpl).render(o=option_value, name=name, message_type=field.message_type, str=str,
                                     field=field, switcher_template=switcher_template, num_template=num_template,
                                     string_template=string_template, bool_template=bool_template)


def rule_type(field):
    name = "p." + field.name
    if has_validate(field) and field.message_type is None:
        for option_descriptor, option_value in field.GetOptions().ListFields():
            if option_descriptor.full_name == "validate.rules":
                if str(option_value.string):
                    return string_template(option_value, name)
                elif str(option_value.message):
                    return message_template(option_value, field.name)
                elif str(option_value.bool):
                    return bool_template(option_value, name)
                elif str(option_value.float):
                    return num_template(option_value, name, option_value.float)
                elif str(option_value.double):
                    return num_template(option_value, name, option_value.double)
                elif str(option_value.int32):
                    return num_template(option_value, name, option_value.int32)
                elif str(option_value.int64):
                    return num_template(option_value, name, option_value.int64)
                elif str(option_value.uint32):
                    return num_template(option_value, name, option_value.uint32)
                elif str(option_value.uint64):
                    return num_template(option_value, name, option_value.uint64)
                elif str(option_value.sint32):
                    return num_template(option_value, name, option_value.sint32)
                elif str(option_value.sint64):
                    return num_template(option_value, name, option_value.sint64)
                elif str(option_value.fixed32):
                    return num_template(option_value, name, option_value.fixed32)
                elif str(option_value.fixed64):
                    return num_template(option_value, name, option_value.fixed64)
                elif str(option_value.sfixed32):
                    return num_template(option_value, name, option_value.sfixed32)
                elif str(option_value.sfixed64):
                    return num_template(option_value, name, option_value.sfixed64)
                elif str(option_value.enum):
                    return enum_template(option_value, name, field)
                elif str(option_value.bytes):
                    return bytes_template(option_value, name)
                elif str(option_value.repeated):
                    return repeated_template(option_value, name, field)
                elif str(option_value.map):
                    return map_template(option_value, name, field)
                elif str(option_value.required):
                    return required_template(option_value, name)
    if field.message_type:
        for option_descriptor, option_value in field.GetOptions().ListFields():
            if option_descriptor.full_name == "validate.rules":
                if str(option_value.duration):
                    return duration_template(option_value, name)
                elif str(option_value.timestamp):
                    return timestamp_template(option_value, name)
                elif str(option_value.float) or str(option_value.int32) or str(option_value.int64) or \
                        str(option_value.double) or str(option_value.uint32) or str(option_value.uint64) or \
                        str(option_value.bool) or str(option_value.string) or str(option_value.bytes):
                    return wrapper_template(option_value, name)
                elif str(option_value.message) != "":
                    return message_template(option_value, field.name)
                elif str(option_value.any):
                    return any_template(option_value, name)
                elif str(option_value.repeated):
                    return repeated_template(option_value, name, field)
                elif str(option_value.map):
                    return map_template(option_value, name, field)
                elif str(option_value.required):
                    return required_template(option_value, name)
        if field.message_type.full_name.startswith("google.protobuf"):
            return ""
        elif is_map(field):
            return map_template(None, name, field)
        elif field.label == 3:
            return repeated_template(None, name, field)
        else:
            return message_template(None, field.name)
    return ""


def file_template(proto_message):
    file_tmp = """
# Validates {{ p.DESCRIPTOR.name }}
def generate_validate(p):
    {%- for option_descriptor, option_value in p.DESCRIPTOR.GetOptions().ListFields() %}
        {%- if option_descriptor.full_name == "validate.disabled" and option_value %}
    return None
        {%- elif option_descriptor.full_name == "validate.ignored" and option_value %}
    return None
        {%- endif -%}
    {%- endfor -%}
    {%- for oneof in p.DESCRIPTOR.oneofs %}
    present = False
        {%- for field in oneof.fields %}
    if _has_field(p, \"{{ field.name }}\"):
        present = True
        {{ rule_type(field)|indent(4,True) }}
        {%- endfor %}
        {% for option in oneof.GetOptions().ListFields() %}
        {% if option[0].name == 'required' and option[1] %}
    if not present:
        raise ValidationFailed(\"Oneof {{ oneof.name }} is required\")
        {% endif %}
        {% endfor %}
    {%- endfor %}
    {%- for field in p.DESCRIPTOR.fields -%}
        {%- if not field.containing_oneof %}
    {{ rule_type(field) -}}
        {%- endif %}
    {%- endfor %}
    return None"""
    return Template(file_tmp).render(rule_type=rule_type, p=proto_message)
