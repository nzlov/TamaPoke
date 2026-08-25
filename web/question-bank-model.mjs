const encoder = new TextEncoder();

function fnv1a(value) {
  let hash = 0x811c9dc5;
  for (const byte of encoder.encode(value)) {
    hash ^= byte;
    hash = Math.imul(hash, 0x01000193);
  }
  return hash >>> 0;
}

export function createPackId(entropy = null) {
  const bytes = entropy ? new Uint8Array(entropy) : new Uint8Array(4);
  if (!entropy) {
    if (globalThis.crypto?.getRandomValues) globalThis.crypto.getRandomValues(bytes);
    else new DataView(bytes.buffer).setUint32(0, fnv1a(`${Date.now()}\0${Math.random()}`));
  }
  if (bytes.length < 4) throw new Error('生成题库 ID 至少需要 4 字节随机数');
  return `quiz-${[...bytes.subarray(0, 4)].map(value => value.toString(16).padStart(2, '0')).join('')}`;
}

export function createQuestionId(question, occupiedIds) {
  const identity = JSON.stringify([
    question.locale,
    question.stem,
    question.options,
    question.answer,
  ]);
  const base = `q-${fnv1a(identity).toString(16).padStart(8, '0')}`;
  const occupied = new Set(occupiedIds);
  if (!occupied.has(base)) return base;
  for (let suffix = 2; suffix < 100000; suffix++) {
    const candidate = `${base}-${suffix}`;
    if (!occupied.has(candidate)) return candidate;
  }
  throw new Error('无法生成唯一题目 ID');
}

export function paginateQuestions(questions, query, requestedPage, pageSize = 10) {
  const needle = query.trim().toLocaleLowerCase();
  const matches = [];
  questions.forEach((question, index) => {
    if (!needle || question.stem.toLocaleLowerCase().includes(needle))
      matches.push({ question, index });
  });
  const totalPages = Math.max(1, Math.ceil(matches.length / pageSize));
  const page = Math.max(1, Math.min(totalPages, Number.isInteger(requestedPage) ? requestedPage : 1));
  const start = (page - 1) * pageSize;
  return {
    items: matches.slice(start, start + pageSize),
    page,
    pageSize,
    totalItems: matches.length,
    totalPages,
  };
}
