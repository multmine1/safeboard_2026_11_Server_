import test from 'node:test';
import assert from 'node:assert/strict';
import {SelectLinks} from '../dist/select_links.mjs';

const links = [
  {label: 'Почта', description: 'Личный ящик', group: 'Работа'},
  {label: 'Grafana', description: 'Метрики сервисов', group: 'Наблюдаемость'},
  {label: 'Cockpit', group: 'Управление'},
];

test('selects by group and searches titles and descriptions without changing the catalogue', () => {
  assert.deepEqual(SelectLinks(links, ' ПОЧТА ', 'Работа'), [links[0]]);
  assert.deepEqual(SelectLinks(links, 'метрики', ''), [links[1]]);
  assert.deepEqual(links.map(link => link.label), ['Почта', 'Grafana', 'Cockpit']);
});

test('empty filters keep all entries and incompatible filters produce an empty view', () => {
  assert.deepEqual(SelectLinks(links, '', ''), links);
  assert.deepEqual(SelectLinks(links, 'почта', 'Управление'), []);
  assert.deepEqual(SelectLinks([], '', ''), []);
});
