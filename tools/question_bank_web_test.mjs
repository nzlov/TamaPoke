import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

import {
  createPackId,
  createQuestionId,
  paginateQuestions,
} from '../web/question-bank-model.mjs';
const serialSource = await readFile(new URL('../web/serial-client.js', import.meta.url), 'utf8');
const { SerialClient } = await import(`data:text/javascript;base64,${Buffer.from(serialSource).toString('base64')}`);

const packId = createPackId(new Uint8Array([0x12, 0x34, 0xab, 0xcd]));
assert.equal(packId, 'quiz-1234abcd');

const draft = {
  locale: 'zh-CN',
  stem: '皮卡丘是什么属性？',
  options: ['电', '火'],
  answer: 0,
};
const firstId = createQuestionId(draft, []);
assert.match(firstId, /^q-[0-9a-f]{8}$/);
assert.equal(createQuestionId(draft, []), firstId);
assert.equal(createQuestionId(draft, [firstId]), `${firstId}-2`);

const questions = Array.from({ length: 23 }, (_, index) => ({
  id: `hidden-${index}`,
  locale: index % 2 ? 'en-US' : 'zh-CN',
  stem: index === 17 ? 'Special Pikachu question' : `普通题干 ${index + 1}`,
  options: ['A', 'B'],
  answer: 0,
}));
let page = paginateQuestions(questions, '', 2, 10);
assert.deepEqual([page.page, page.totalPages, page.totalItems, page.items.length], [2, 3, 23, 10]);
assert.equal(page.items[0].index, 10);
page = paginateQuestions(questions, '  PIKACHU ', 9, 10);
assert.deepEqual([page.page, page.totalPages, page.totalItems, page.items.length], [1, 1, 1, 1]);
assert.equal(page.items[0].index, 17);
assert.equal(paginateQuestions(questions, 'hidden-17', 1, 10).totalItems, 0,
  'search is limited to the visible question stem');

const client = new SerialClient();
const encoder = new TextEncoder();
const payload = new Uint8Array([0, 10, 13, 255, 65]);
client.writer = {
  async write(bytes) {
    const command = new TextDecoder().decode(bytes);
    if (command === 'GET /packs/example.tquiz\n') {
      queueMicrotask(() => client.consume(encoder.encode(`FILE ${payload.length}\n`)));
    } else if (command === 'OK\n') {
      const response = new Uint8Array(payload.length + 5);
      response.set(payload);
      response.set(encoder.encode('DONE\n'), payload.length);
      queueMicrotask(() => client.consume(response));
    }
  },
};
const progress = [];
assert.deepEqual(await client.readFile('/packs/example.tquiz', value => progress.push(value)), payload);
assert.equal(progress.at(-1), payload.length);

console.log('PASS question-bank web model and binary device import');
