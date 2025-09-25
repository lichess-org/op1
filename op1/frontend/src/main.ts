import { h, init, VNode, classModule } from 'snabbdom';
import page from 'page';

class Controller {
    text: string;
}

function run(element: Element) {
    const patch = init([classModule]);

    let vnode: VNode;
    const ctrl = new Controller();

    function redraw() {
        vnode = patch(vnode || element, render());
    }

    function render() {
      return h('div#app', [
        h('h1', ctrl.text)
      ])
    }

    page('/', () => {
        ctrl.text = 'Hello world!';
        redraw();
    });
    page('/meta', () => {
        ctrl.text = 'Meta';
        redraw();
    });
    page('*', () => {
        ctrl.text = 'Page not found';
        redraw();
    });
    page({hashbang: true});
}


run(document.getElementById('app')!);
