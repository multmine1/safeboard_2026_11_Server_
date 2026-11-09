import {SelectLinks} from './select_links.mjs';

const grid = document.querySelector('#services');
const status = document.querySelector('#status');
const search = document.querySelector('#search');
const filters = [...document.querySelectorAll('[data-group]')];
let links = [];
let group = '';

function render() {
  const selected = SelectLinks(links, search.value, group);
  grid.replaceChildren();
  for (const link of selected) {
    const card = document.createElement('a');
    card.className = 'card';
    card.href = link.url;
    card.target = '_blank';
    card.rel = 'noopener noreferrer';
    const head = document.createElement('div');
    head.className = 'card-head';
    const icon = document.createElement('span');
    icon.className = 'icon';
    icon.textContent = link.icon ?? link.label[0];
    const arrow = document.createElement('span');
    arrow.className = 'arrow';
    arrow.textContent = '↗';
    arrow.setAttribute('aria-hidden', 'true');
    head.append(icon, arrow);
    const title = document.createElement('h2');
    title.textContent = link.label;
    const description = document.createElement('p');
    description.textContent = link.description ?? '';
    const origin = document.createElement('div');
    origin.className = 'origin';
    origin.textContent = new URL(link.url).host;
    card.append(head, title, description, origin);
    grid.append(card);
  }
  status.textContent = selected.length ? `Доступные ссылки: ${selected.length}` : 'Нет подходящих сервисов';
}

for (const button of filters) button.addEventListener('click', () => {
  group = button.dataset.group;
  for (const filter of filters) filter.setAttribute('aria-pressed', String(filter === button));
  render();
});
search.addEventListener('input', render);
fetch('/links.json', {cache: 'no-store'})
  .then(response => { if (!response.ok) throw new Error('navigation'); return response.json(); })
  .then(data => { links = data; render(); })
  .catch(() => { status.textContent = 'Список сервисов недоступен. Обновите страницу после проверки конфигурации.'; });

