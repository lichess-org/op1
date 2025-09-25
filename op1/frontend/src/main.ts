import { h, init, VNode, classModule } from 'snabbdom';

function run(element: Element) {
    const patch = init([classModule]);

    let vnode: VNode;

    function redraw() {
        vnode = patch(vnode || element, render());
    }

    function render() {
      return h('div#app', [
        h('h1', 'Hello World!')
      ])
    }

    redraw();
}

run(document.getElementById('app')!);
