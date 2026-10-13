{%- if untrusted_downstream %}
This field should be configured in the presence of untrusted *downstreams*.
{% endif %}
{%- if untrusted_upstream %}
This field should be configured in the presence of untrusted *upstreams*.
{%- endif -%}
{%- if note %}
{{ note }}
{%- endif %}

Example configuration for untrusted environments:

.. code-block:: yaml

  {{ example | indent(2) }}
