(function () {
  if (window.__ancoreShowCtxMenu) return;
  window.__ancoreCtxInit = true;

  var css = document.createElement('style');
  css.textContent = [
    '#ancore-ctx-root{position:fixed;inset:0;z-index:2147483647;display:none;font:12px/1.4 "Segoe UI",-apple-system,sans-serif}',
    '#ancore-ctx-root.open{display:block}',
    '#ancore-ctx-menu{position:fixed;min-width:228px;max-width:340px;background:#1A1A1A;color:#E5E2E1;border:1px solid #333;border-radius:12px;padding:6px 0;box-shadow:0 12px 36px rgba(0,0,0,.62),0 0 0 1px rgba(124,77,255,.1);user-select:none;overflow:hidden}',
    '#ancore-ctx-menu .ctx-item{padding:9px 16px;cursor:pointer;border-radius:8px;margin:0 6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;transition:background .12s ease,color .12s ease}',
    '#ancore-ctx-menu .ctx-item:hover:not(.disabled){background:#353535;color:#fff}',
    '#ancore-ctx-menu .ctx-item.disabled{color:#666;cursor:default;opacity:.55}',
    '#ancore-ctx-menu .ctx-sep{height:1px;background:#333;margin:6px 12px}'
  ].join('');
  (document.head || document.documentElement).appendChild(css);

  var root = document.createElement('div');
  root.id = 'ancore-ctx-root';
  var menu = document.createElement('div');
  menu.id = 'ancore-ctx-menu';
  root.appendChild(menu);
  document.documentElement.appendChild(root);

  function post(cmd) {
    try {
      if (window.__ancorePost) window.__ancorePost(cmd);
      else {
        var wv = window.chrome && window.chrome.webview;
        if (wv) wv.postMessage(cmd);
      }
    } catch (e) {}
  }

  function hide() {
    root.classList.remove('open');
    menu.innerHTML = '';
  }

  function clampMenu(x, y) {
    menu.style.left = x + 'px';
    menu.style.top = y + 'px';
    var r = menu.getBoundingClientRect();
    var nx = x, ny = y;
    if (r.right > window.innerWidth - 8) nx = Math.max(8, window.innerWidth - r.width - 8);
    if (r.bottom > window.innerHeight - 8) ny = Math.max(8, window.innerHeight - r.height - 8);
    menu.style.left = nx + 'px';
    menu.style.top = ny + 'px';
  }

  window.__ancoreShowCtxMenu = function (x, y, itemsOrJson) {
    var items = itemsOrJson;
    if (typeof itemsOrJson === 'string') {
      try { items = JSON.parse(itemsOrJson); } catch (e) { return; }
    }
    if (!items || !items.length) return;
    menu.innerHTML = '';
    for (var i = 0; i < items.length; i++) {
      var it = items[i];
      if (it.sep) {
        var s = document.createElement('div');
        s.className = 'ctx-sep';
        menu.appendChild(s);
        continue;
      }
      var el = document.createElement('div');
      el.className = 'ctx-item' + (it.disabled ? ' disabled' : '');
      el.textContent = it.label || '';
      if (!it.disabled && it.id) {
        (function (id) {
          el.addEventListener('mousedown', function (ev) {
            ev.preventDefault();
            ev.stopPropagation();
          });
          el.addEventListener('click', function (ev) {
            ev.stopPropagation();
            hide();
            post('ctx:' + id);
          });
        })(it.id);
      }
      menu.appendChild(el);
    }
    root.classList.add('open');
    clampMenu(x, y);
  };

  root.addEventListener('mousedown', function (e) {
    if (e.target === root) hide();
  });
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') hide();
  }, true);
  window.__ancoreHideCtxMenu = hide;
})();
