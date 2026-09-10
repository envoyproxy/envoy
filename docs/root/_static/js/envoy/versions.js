/** Version menu: close on outside click, on Escape, and when another opens. */

export function init() {
  const menus = Array.from(document.querySelectorAll('.envoy-version-menu'));
  if (!menus.length) {
    return;
  }

  menus.forEach((menu) => {
    menu.addEventListener('toggle', () => {
      if (!menu.open) {
        return;
      }
      menus.filter((other) => other !== menu)
        .forEach((other) => other.removeAttribute('open'));
    });
  });

  document.addEventListener('click', (event) => {
    menus.forEach((menu) => {
      if (menu.open && !menu.contains(event.target)) {
        menu.removeAttribute('open');
      }
    });
  });

  document.addEventListener('keydown', (event) => {
    if (event.key !== 'Escape') {
      return;
    }
    menus.forEach((menu) => {
      if (menu.open) {
        menu.removeAttribute('open');
        menu.querySelector('summary')?.focus();
      }
    });
  });
}
