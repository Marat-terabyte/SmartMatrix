#pragma once

#include <Arduino.h>

namespace draw_http {

const char DrawPage[] PROGMEM = R"HTML(<!doctype html>
<html lang="ru">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SmartMatrix — рисование</title>
<style>
body{font:16px system-ui,sans-serif;max-width:36rem;margin:2rem auto;padding:0 1rem}
label,input,textarea{display:block}label{margin:.8rem 0}
input,textarea{font:inherit;width:100%;box-sizing:border-box}button{font:inherit}
canvas{width:min(100%,256px);height:auto;image-rendering:pixelated;background:#000}
</style>
<h1>SmartMatrix</h1>
<p><a href="/wifi">Статус Wi-Fi</a></p>
<details id="settings">
<summary>Настройки API</summary>
<p>Браузер передаёт ключ плате по HTTP. Используйте доверенную сеть.
Настройки хранятся до перезагрузки платы. При сохранении пустой ключ заменит прежний.</p>
<form id="config" method="post" autocomplete="off">
<label>Базовый URL API <input id="baseUrl" type="url" maxlength="240" placeholder="https://api.openai.com/v1" required></label>
<label>ID модели <input id="model" maxlength="128" required></label>
<label>Ключ API (если требуется) <input id="apiKey" type="password" maxlength="512" autocomplete="new-password"></label>
<label>Свой корневой CA для HTTPS (необязательно, PEM)
<textarea id="ca" rows="3" maxlength="4096" spellcheck="false"></textarea></label>
<button id="save">Сохранить настройки</button>
</form>
</details>
<form id="draw" method="post">
<label>Что нарисовать? <textarea id="prompt" rows="3" maxlength="768" required placeholder="Например, красный гриб"></textarea></label>
<button id="send" disabled>Нарисовать</button>
</form>
<p id="status" role="status" aria-live="polite">Проверяю настройки…</p>
<canvas id="preview" width="16" height="16" aria-label="Превью последнего кадра" hidden></canvas>
<noscript>Для отправки промпта нужен JavaScript.</noscript>
<script src="/draw.js" defer></script>
</html>
)HTML";

const char DrawScript[] PROGMEM = R"JS(
'use strict';
const el = id => document.getElementById(id);
const status = el('status');
const settings = el('settings');
let configured = false;
function busy(value) {
  el('save').disabled = value;
  el('send').disabled = value || !configured;
}
async function call(path, payload) {
  const options = payload === undefined ? {} : {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(payload)
  };
  const response = await fetch(path, options);
  if (!response.ok) {
    if (path === '/api/draw' && response.status === 409) {
      configured = false;
      settings.open = true;
    }
    throw new Error((await response.text()) || `Ошибка платы: HTTP ${response.status}.`);
  }
  return response.status === 204 ? null : response.json();
}
function showFrame(rgb) {
  if (typeof rgb !== 'string' || !/^[0-9a-f]{1536}$/.test(rgb)) {
    throw new Error('Плата вернула некорректное превью.');
  }
  const canvas = el('preview');
  const ctx = canvas.getContext('2d');
  for (let i = 0; i < 256; i++) {
    ctx.fillStyle = '#' + rgb.slice(i * 6, i * 6 + 6);
    ctx.fillRect(i % 16, Math.floor(i / 16), 1, 1);
  }
  canvas.hidden = false;
}
el('config').addEventListener('submit', async event => {
  event.preventDefault();
  busy(true);
  status.textContent = 'Сохраняю настройки…';
  const apiKey = el('apiKey');
  try {
    await call('/api/config', {
      baseUrl: el('baseUrl').value.trim(), model: el('model').value.trim(),
      apiKey: apiKey.value.trim(), ca: el('ca').value.trim()
    });
    apiKey.value = '';
    configured = true;
    settings.open = false;
    status.textContent = 'Настройки сохранены. Введите промпт.';
    el('prompt').focus();
  } catch (error) {
    status.textContent = error.message;
  } finally {
    busy(false);
  }
});
el('draw').addEventListener('submit', async event => {
  event.preventDefault();
  if (!configured || el('send').disabled) return;
  const prompt = el('prompt').value.trim();
  if (!prompt || new TextEncoder().encode(prompt).length > 768) {
    status.textContent = 'Введите промпт длиной до 768 байт UTF-8.';
    return;
  }
  busy(true);
  status.textContent = 'Запрашиваю кадр у модели…';
  try {
    const frame = await call('/api/draw', {prompt});
    showFrame(frame.rgb);
    status.textContent = frame.lit === 0
      ? 'Кадр полностью чёрный: матрица погашена.'
      : `Кадр выведен. Ненулевых пикселей: ${frame.lit}/256.`;
  } catch (error) {
    status.textContent = error.message;
  } finally {
    busy(false);
  }
});
busy(true);
call('/api/config').then(state => {
  configured = state.configured === true;
  settings.open = !configured;
  status.textContent = configured ? 'Введите промпт.' : 'Сначала сохраните настройки API.';
}).catch(error => {
  settings.open = true;
  status.textContent = error.message;
}).finally(() => busy(false));
)JS";

}  // namespace draw_http
