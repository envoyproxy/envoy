/**
 * Code block headers.
 *
 * Sphinx renders a `:caption:` as a separate bar above the block and puts the
 * lexer in a class on the block itself, so a reader gets either a caption with
 * no language or a language with no context. This merges the two into one
 * header rail: the file name on the left, the lexer on the right.
 */

/** Lexer aliases that read better than the name Pygments was asked for. */
const LANGUAGE_NAMES = {
  'c++': 'cpp',
  console: 'console',
  default: 'text',
  ini: 'ini',
  jsonc: 'json',
  none: 'text',
  protobuf: 'proto',
  shell: 'shell',
  'shell-session': 'console',
  text: 'text',
};

function languageOf(block) {
  const match = Array.from(block.classList)
    .map((name) => /^highlight-(.+)$/.exec(name))
    .find(Boolean);

  if (!match) {
    return null;
  }

  const raw = match[1].toLowerCase();
  return LANGUAGE_NAMES[raw] || raw;
}

function buildHead(name, language) {
  const head = document.createElement('div');
  head.className = 'envoy-code-head';

  if (name) {
    const label = document.createElement('span');
    label.className = 'envoy-code-name';
    label.textContent = name;
    head.append(label);
  }

  if (language) {
    const tag = document.createElement('span');
    tag.className = 'envoy-code-lang';
    tag.textContent = language;
    head.append(tag);
  }

  return head;
}

export function init() {
  const root = document.querySelector('.envoy-content-main');
  if (!root) {
    return;
  }

  root.querySelectorAll('div[class*="highlight-"]').forEach((block) => {
    const language = languageOf(block);
    const wrapper = block.closest('.literal-block-wrapper');
    const caption = wrapper ? wrapper.querySelector('.code-block-caption') : null;
    const captionText = caption ?
      caption.querySelector('.caption-text')?.textContent.trim() : null;

    // A block with neither a caption nor a recognisable lexer has nothing worth
    // putting in a header.
    if (!captionText && (!language || language === 'text')) {
      return;
    }

    const head = buildHead(captionText, language);
    const container = wrapper || block;
    container.prepend(head);
    container.classList.add('envoy-has-code-head');
  });
}
