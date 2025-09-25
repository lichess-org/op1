import { h, init, VNode, classModule } from 'snabbdom';
import page from 'page';

const backend = 'http://localhost:9999';

class Controller {
  metaKeys: string[];

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

function view(ctrl: Controller): VNode {
  return h('div#app', [h('h1', ctrl.metaKeys)]);
}

function run(element: Element) {
  const patch = init([classModule]);

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
    ctrl.text = 'Meta';
    redraw();
  });
  page('*', () => {
    ctrl.text = 'Page not found';
    redraw();
  });
  page({ hashbang: true });
}

run(document.getElementById('app')!);
