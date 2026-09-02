/**
 * Generated proto reference pages: kind rails, dimmed packages, outline dots.
 *
 * protodoc emits every symbol as an ordinary section with an ordinary heading,
 * so message, enum and service are indistinguishable on the page. This marks
 * each section with its kind, which the stylesheet turns into a coloured rail
 * running the full height of the symbol.
 *
 * The kind is read from the markup when it is there — `data-envoy-kind` on the
 * section, or an `envoy-kind-*` class from a `.. rst-class::` directive — and
 * inferred from the heading otherwise. protodoc's output format is fixed
 * ("Enum <name>" for enums, a bare dotted name for messages), so the inference
 * is reliable today; emitting the kind from the generator would remove the
 * guess entirely and is the natural follow-up.
 */

const KINDS = ['message', 'enum', 'service'];

/** A dotted proto symbol: a lower-case package then CamelCase segments. */
const SYMBOL = /^(?:[a-z][\w]*\.)+[A-Z]\w*(?:\.[A-Z]\w*)*$/;

/** protodoc titles the two non-message kinds explicitly. */
const TITLED_KINDS = [
  [/^Enum\s+/i, 'enum'],
  [/^Service\s+/i, 'service'],
];

const HEADINGS = ':scope > h1, :scope > h2, :scope > h3, :scope > h4, :scope > h5';

/** protodoc annotates a required field as "(<type>, *REQUIRED*)". */
const REQUIRED = /^\s*REQUIRED\s*$/;

function headingOf(section) {
  return section.querySelector(HEADINGS);
}

/**
 * The heading's own text, without the permalink.
 *
 * sphinx_rtd_theme puts a Font Awesome glyph inside `a.headerlink` as a real
 * character rather than generated content, so `textContent` on the heading
 * comes back with a U+F0C1 on the end and nothing matches.
 */
function titleOf(heading) {
  return Array.from(heading.childNodes)
    .filter((node) => !(node.nodeType === Node.ELEMENT_NODE &&
      node.classList.contains('headerlink')))
    .map((node) => node.textContent)
    .join('')
    .trim();
}

/**
 * The kind of a symbol section, or null if this section is not one.
 * Markup wins over inference so a future generator change takes effect without
 * touching this file.
 */
function kindOf(section, heading) {
  if (KINDS.includes(section.dataset.envoyKind)) {
    return section.dataset.envoyKind;
  }

  const declared = KINDS.find((kind) => section.classList.contains(`envoy-kind-${kind}`));
  if (declared) {
    return declared;
  }

  const text = titleOf(heading);
  const titled = TITLED_KINDS.find(([pattern]) => pattern.test(text));
  if (titled) {
    return titled[1];
  }

  return SYMBOL.test(text) ? 'message' : null;
}

/** Strips the kind word protodoc puts in front of enum and service titles. */
function symbolOf(text) {
  return TITLED_KINDS.reduce((name, [pattern]) => name.replace(pattern, ''), text);
}

/** Rewrites the heading as a dimmed package plus the identifying tail. */
function dimPackage(heading, symbol) {
  const match = /^(?:[a-z][\w]*\.)+/.exec(symbol);
  if (!match) {
    return;
  }

  const anchor = heading.querySelector('a.headerlink');
  const pkg = document.createElement('span');
  pkg.className = 'envoy-proto-package';
  pkg.textContent = match[0];

  heading.replaceChildren(pkg, symbol.slice(match[0].length));
  if (anchor) {
    heading.append(anchor);
  }
}

/**
 * Styles protodoc's "*REQUIRED*" annotation as a marker.
 *
 * The annotation is marked where it already sits — inside the generated
 * parenthetical next to the field type — rather than being moved to the field
 * name. Moving it would mean either editing prose this file did not write or
 * printing "required" twice on every required field.
 */
function markRequired(section) {
  section.querySelectorAll(':scope > dl > dd em').forEach((emphasis) => {
    if (REQUIRED.test(emphasis.textContent)) {
      emphasis.classList.add('envoy-proto-required');
    }
  });
}

/** Repeats each symbol's kind as a dot in the page outline. */
function markOutline(toc, anchors) {
  toc.querySelectorAll('a[href^="#"]').forEach((link) => {
    const kind = anchors.get(decodeURIComponent(link.getAttribute('href').slice(1)));
    if (!kind || link.querySelector('.envoy-toc-dot')) {
      return;
    }

    const dot = document.createElement('span');
    dot.className = `envoy-toc-dot envoy-kind-${kind}`;
    dot.setAttribute('aria-hidden', 'true');
    link.prepend(dot);
    // The section itself carries a visible MESSAGE / ENUM label; in the outline
    // the kind is a secondary cue, so it goes in the tooltip rather than adding
    // a second badge next to every entry.
    link.title = link.title ? `${link.title} · ${kind}` : kind;
  });
}

export function init() {
  const content = document.querySelector('.envoy-content-main .rst-content');
  if (!content) {
    return;
  }

  const anchors = new Map();
  let symbols = 0;

  content.querySelectorAll('section').forEach((section) => {
    const heading = headingOf(section);
    if (!heading) {
      return;
    }

    const kind = kindOf(section, heading);
    if (!kind) {
      return;
    }

    symbols += 1;
    section.classList.add('envoy-proto-symbol', `envoy-kind-${kind}`);
    if (section.id) {
      anchors.set(section.id, kind);
    }

    const label = document.createElement('span');
    label.className = 'envoy-proto-kind';
    label.textContent = kind;
    section.insertBefore(label, heading);

    const symbol = symbolOf(titleOf(heading));
    heading.title = symbol;
    dimPackage(heading, symbol);
    markRequired(section);
  });

  // A page with one stray dotted heading is not a reference page.
  if (symbols < 2) {
    content.querySelectorAll('.envoy-proto-symbol').forEach((section) => {
      section.classList.remove('envoy-proto-symbol', ...KINDS.map((k) => `envoy-kind-${k}`));
      section.querySelector(':scope > .envoy-proto-kind')?.remove();
    });
    return;
  }

  // Reference pages are scanned in columns, not read in a measure.
  content.classList.add('envoy-reference');

  const toc = document.querySelector('.envoy-page-toc');
  if (toc) {
    markOutline(toc, anchors);
  }
}
