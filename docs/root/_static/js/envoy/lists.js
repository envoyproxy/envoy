/**
 * Lists of links: toctree indexes rendered as rows rather than paragraphs.
 *
 * `api-v3/bootstrap/bootstrap.html` is 74 hyperlinks in one column, every one
 * of them opening with the same twenty-one characters, all set in the prose
 * face and all underlined. Nothing here moves, groups, sorts or renames any of
 * them — the order Sphinx emits is the order that renders. What changes is:
 *
 *   1. identifiers are set in mono, so the repeated prefix stacks into a
 *      column and the segments that differ line up down the page;
 *   2. the prefix is dimmed rather than removed — every character is still
 *      there, still selectable, still copyable;
 *   3. the kind dot replaces protodoc's literal "Enum " prefix, so every name
 *      starts at the same x-position;
 *   4. the row, not the text, carries the link affordance — the accent and the
 *      underline arrive on hover.
 */

/** A dotted proto symbol: a lower-case package then CamelCase segments. */
const SYMBOL = /^(?:[a-z][\w]*\.)+[A-Z]\w*(?:\.[A-Z]\w*)*$/;

/** protodoc titles the two non-message kinds explicitly. */
const TITLED_KINDS = [
  [/^Enum\s+/i, 'enum'],
  [/^Service\s+/i, 'service'],
];

/** Everything before the first capitalised segment. */
const PACKAGE = /^(?:[a-z][\w]*\.)+(?:[A-Z]\w*\.)*/;

function classify(text) {
  let kind = 'message';
  let symbol = text;

  const titled = TITLED_KINDS.find(([pattern]) => pattern.test(symbol));
  if (titled) {
    kind = titled[1];
    symbol = symbol.replace(titled[0], '');
  }

  return SYMBOL.test(symbol) ? {kind, symbol} : null;
}

/**
 * Rewrites a link as a dimmed prefix plus the identifying tail.
 * The full symbol goes in the tooltip; the href is untouched.
 */
function render(link, entry) {
  const match = PACKAGE.exec(entry.symbol);
  const prefix = match ? match[0] : '';

  const dot = document.createElement('span');
  dot.className = `envoy-list-dot envoy-kind-${entry.kind}`;
  dot.setAttribute('aria-hidden', 'true');

  const name = document.createElement('span');
  name.className = 'envoy-list-name';
  name.textContent = entry.symbol.slice(prefix.length);

  link.replaceChildren(dot);
  if (prefix) {
    const muted = document.createElement('span');
    muted.className = 'envoy-list-prefix';
    muted.textContent = prefix;
    link.append(muted);
  }
  link.append(name);

  link.classList.add('envoy-list-symbol');
  link.title = entry.symbol;
}

function decorate(wrapper) {
  const links = Array.from(wrapper.querySelectorAll('li > a'));
  let symbols = 0;

  links.forEach((link) => {
    // The file link that heads each group keeps its prose title.
    const isHeader = link.parentElement.classList.contains('toctree-l1');
    const entry = isHeader ? null : classify(link.textContent.trim());

    if (isHeader) {
      link.classList.add('envoy-list-head');
      return;
    }

    if (entry) {
      symbols += 1;
      render(link, entry);
    }
  });

  // Only switch a list into row mode when it is actually an index of symbols;
  // a three-item toctree on a landing page reads better as prose links.
  if (symbols >= 4) {
    wrapper.classList.add('envoy-list-rows');
  }
}

export function init() {
  document
    .querySelectorAll('.envoy-content-main .rst-content .toctree-wrapper')
    .forEach(decorate);
}
