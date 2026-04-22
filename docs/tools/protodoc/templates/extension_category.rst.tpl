.. _extension_category_{{category}}:

.. tip::
{% if extensions %}
  This extension category has the following known extensions:

{% for ext in extensions %}
  - :ref:`{{ext}} <extension_{{ext}}>`
{% endfor %}

{% endif %}
{% if contrib_extensions %}
  The following extensions are available in :ref:`contrib <install_contrib>` images only:

{% for ext in contrib_extensions %}
  - :ref:`{{ext}} <extension_{{ext}}>`
{% endfor %}
{% endif %}
