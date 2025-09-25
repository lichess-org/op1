import { h, init, VNode, classModule, attributesModule } from 'snabbdom';
import page from 'page';

const backend = 'http://localhost:9999';

class Controller {
  metaKeys: string[] = [];

  constructor(readonly redraw: () => void) {
    this.redraw = redraw;
  }

  showMetaKeys() {
    fetch(`${backend}/api/meta`)
      .then(response => response.json())
      .then(data => {
        this.metaKeys = data;
        this.redraw();
      });
  }
}

function viewMetaKey(key: string): VNode {
  const [material, ...parts] = key.split('_');
  const bk = material.lastIndexOf('k');
  const white = material.substring(0, bk);
  const black = material.substring(bk);
  return h(
    'a.key',
    {
      attrs: {
        href: `#!/meta/${key}`,
      },
    },
    [h('span.w', white), h('span.b', black), ...parts.map(part => h('span', `_${part}`))],
  );
}

function view(ctrl: Controller): VNode {
  return h('div#app', [
    h('h1', 'Tables'),
    h(
      'ul',
      ctrl.metaKeys.map(key => viewMetaKey(key)),
    ),
  ]);
}

function run(element: Element) {
  const patch = init([classModule, attributesModule]);

  let vnode: VNode;

  function redraw() {
    vnode = patch(vnode || element, render());
  }

  const ctrl = new Controller(redraw);

  function render() {
    return view(ctrl);
  }

  page('/', () => {
    ctrl.showMetaKeys();
  });
  page('/meta', () => {
    ctrl.showMetaKeys();
    redraw();
  });
  page('/meta/:key', ({ params }) => {
    ctrl.showMetaKeys();
    redraw();
  });
  page('*', () => {
    ctrl.showMetaKeys();
    redraw();
  });
  page({ hashbang: true });
}

run(document.getElementById('app')!);
