const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { test } = require('node:test');
const { setImmediate: flush } = require('node:timers/promises');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '../../firmware/SmartMatrix/DrawPage.h'), 'utf8');
const html = source.match(/R"HTML\(([\s\S]*?)\)HTML"/)[1];
const script = source.match(/R"JS\(([\s\S]*?)\)JS"/)[1];

function page(replies) {
  const elements = Object.fromEntries(Array.from(html.matchAll(/ id="([^"]+)"/g), ([, id]) => [id, {
    value: '',
    checked: false,
    disabled: false,
    hidden: id === 'preview',
    focus() {},
    addEventListener(event, handler) { this[event] = handler; }
  }]));
  const pixels = [];
  const canvas = {
    fillRect(x, y, width, height) { pixels.push([x, y, width, height, this.fillStyle]); }
  };
  elements.preview.getContext = type => type === '2d' ? canvas : null;
  const requests = [];
  vm.runInNewContext(script, {
    document: { getElementById: id => elements[id] },
    TextEncoder,
    fetch: async (url, options) => {
      requests.push([url, options.method || 'GET', options.body && JSON.parse(options.body)]);
      assert.ok(replies.length, `unexpected fetch: ${url}`);
      return replies.shift();
    }
  });
  const submit = id => elements[id].submit({ preventDefault() {} });
  return { elements, requests, pixels, submit };
}

const configState = configured => Response.json({ configured });
const redPixel = () => Response.json({ rgb: 'ff0000' + '000000'.repeat(255), lit: 1 });

test('settings go once; two prompts contain no credentials and render the RGB preview', async () => {
  const { elements: e, requests, pixels, submit } = page([
    configState(false), new Response(null, { status: 204 }), redPixel(),
    Response.json({ rgb: '000000'.repeat(256), lit: 0 })
  ]);
  await flush();
  assert.equal(e.settings.open, true);
  assert.equal(e.send.disabled, true);
  e.baseUrl.value = 'https://provider.test/v1';
  e.model.value = 'drawing-model';
  e.apiKey.value = 'fixture-token';
  await submit('config');
  assert.equal(e.apiKey.value, '');
  assert.equal(e.send.disabled, false);
  e.prompt.value = ' красный пиксель ';
  await submit('draw');
  assert.equal(e.preview.hidden, false);
  assert.match(e.status.textContent, /1\/256/);
  assert.deepEqual(pixels[0], [0, 0, 1, 1, '#ff0000']);
  assert.deepEqual(pixels[255], [15, 15, 1, 1, '#000000']);
  e.prompt.value = 'погаси';
  await submit('draw');
  assert.match(e.status.textContent, /полностью чёрный/);
  assert.equal(pixels.length, 512);
  assert.deepEqual(requests, [
    ['/api/config', 'GET', undefined],
    ['/api/config', 'POST', { baseUrl: 'https://provider.test/v1', model: 'drawing-model',
      apiKey: 'fixture-token', ca: '', insecureTls: false }],
    ['/api/draw', 'POST', { prompt: 'красный пиксель' }],
    ['/api/draw', 'POST', { prompt: 'погаси' }]
  ]);
});

test('ignoring certificates is opt-in and skips the custom CA', async () => {
  const { elements: e, requests, submit } = page([
    configState(false), new Response(null, { status: 204 }), new Response(null, { status: 204 })
  ]);
  await flush();
  assert.equal(e.insecureTls.checked, false);
  assert.equal(e.ca.disabled, false);
  e.baseUrl.value = 'https://local-provider.test/v1';
  e.model.value = 'drawing-model';
  e.ca.value = 'fixture PEM';
  e.insecureTls.checked = true;
  e.insecureTls.change();
  assert.equal(e.ca.disabled, true);
  await submit('config');
  assert.deepEqual(requests[1][2], {
    baseUrl: 'https://local-provider.test/v1', model: 'drawing-model',
    apiKey: '', ca: '', insecureTls: true
  });
  e.insecureTls.checked = false;
  e.insecureTls.change();
  assert.equal(e.ca.disabled, false);
  await submit('config');
  assert.equal(requests[2][2].ca, 'fixture PEM');
  assert.equal(requests[2][2].insecureTls, false);
});

test('provider errors preserve preview; reboot reopens settings without a key', async () => {
  const { elements: e, pixels, submit } = page([
    configState(true), redPixel(), new Response('Неверный кадр', { status: 502 }),
    new Response('Сначала сохраните настройки API.', { status: 409 })
  ]);
  await flush();
  assert.equal(e.settings.open, false);
  e.prompt.value = 'проверка';
  await submit('draw');
  await submit('draw');
  assert.equal(e.status.textContent, 'Неверный кадр');
  assert.equal(e.preview.hidden, false);
  assert.equal(pixels.length, 256);
  await submit('draw');
  assert.equal(e.settings.open, true);
  assert.equal(e.send.disabled, true);
  assert.equal(e.apiKey.value, '');
});
