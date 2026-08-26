const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

const COMMON_SIZE = 48;
const SECTION_SIZE = 16;
const RECORD_SIZE = 14;
const PACK_ABI = 3;
const PACK_KIND_QUIZ = 4;
const PACK_ID = /^[a-z0-9][a-z0-9._-]{0,18}$/;
const LOCALE = /^[A-Za-z0-9-]{2,15}$/;

const CRC32_TABLE = new Uint32Array(256);
for (let value = 0; value < CRC32_TABLE.length; value++) {
  let crc = value;
  for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
  CRC32_TABLE[value] = crc >>> 0;
}

function crc32(data) {
  let crc = 0xFFFFFFFF;
  for (const value of data) crc = CRC32_TABLE[(crc ^ value) & 0xFF] ^ (crc >>> 8);
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

function encoded(value, field, maximum) {
  if (typeof value !== 'string' || !value) throw new Error(`${field}不能为空`);
  if (value.includes('\0')) throw new Error(`${field}不能包含 NUL`);
  const raw = encoder.encode(value);
  if (raw.length > maximum) throw new Error(`${field}超过 ${maximum} 个 UTF-8 字节`);
  return raw;
}

export function normalizeQuestionBank(document) {
  if (!document || typeof document !== 'object' || Array.isArray(document) || document.schema !== 1)
    throw new Error('题库 schema 必须为 1');
  if (typeof document.id !== 'string' || !PACK_ID.test(document.id))
    throw new Error('包 ID 必须匹配 [a-z0-9][a-z0-9._-]{0,18}');
  const revision = document.revision ?? 1;
  if (!Number.isInteger(revision) || revision <= 0 || revision > 0xFFFFFFFF)
    throw new Error('版本必须是正的 32 位整数');
  const label = typeof (document.label ?? document.id) === 'string'
    ? (document.label ?? document.id).trim() : '';
  if (!label) throw new Error('包名称不能为空');
  if (!Array.isArray(document.questions) || !document.questions.length)
    throw new Error('题库至少需要一道题');

  const identities = new Set();
  const questions = document.questions.map((item, position) => {
    const number = position + 1;
    if (!item || typeof item !== 'object' || Array.isArray(item))
      throw new Error(`第 ${number} 题必须是对象`);
    const idRaw = encoded(item.id, `第 ${number} 题 ID`, 40);
    if (typeof item.locale !== 'string' || !LOCALE.test(item.locale))
      throw new Error(`第 ${number} 题语言代码无效`);
    const identity = `${item.locale}\0${item.id}`;
    if (identities.has(identity)) throw new Error(`语言 ${item.locale} 下题目 ID ${item.id} 重复`);
    identities.add(identity);
    const stemRaw = encoded(item.stem, `第 ${number} 题题干`, 768);
    if (!Array.isArray(item.options) || item.options.length < 2 || item.options.length > 4)
      throw new Error(`第 ${number} 题必须有 2 至 4 个选项`);
    const optionRaws = item.options.map((value, index) =>
      encoded(value, `第 ${number} 题选项 ${index + 1}`, 192));
    if (!Number.isInteger(item.answer) || item.answer < 0 || item.answer >= item.options.length)
      throw new Error(`第 ${number} 题正确答案无效`);
    return {
      id: item.id,
      locale: item.locale,
      stem: item.stem,
      options: [...item.options],
      answer: item.answer,
      idRaw,
      stemRaw,
      optionRaws,
    };
  });
  const compareCodepoints = (left, right) => {
    const a = [...left], b = [...right];
    for (let index = 0; index < Math.min(a.length, b.length); index++) {
      const difference = a[index].codePointAt(0) - b[index].codePointAt(0);
      if (difference) return difference;
    }
    return a.length - b.length;
  };
  questions.sort((left, right) => compareCodepoints(left.locale, right.locale) ||
    compareCodepoints(left.id, right.id));
  return { schema: 1, id: document.id, label, revision, questions };
}

function fourcc(view, offset, value) {
  for (let index = 0; index < 4; index++) view.setUint8(offset + index, value.charCodeAt(index));
}

function concatenate(parts) {
  const size = parts.reduce((total, part) => total + part.length, 0);
  const result = new Uint8Array(size);
  let offset = 0;
  for (const part of parts) { result.set(part, offset); offset += part.length; }
  return result;
}

export function buildQuestionPack(document) {
  const bank = normalizeQuestionBank(document);
  const localeRows = [];
  const indexRows = [];
  const records = [];
  let first = 0, dataOffset = 0;
  for (const locale of [...new Set(bank.questions.map(question => question.locale))].sort()) {
    const questions = bank.questions.filter(question => question.locale === locale);
    localeRows.push({ locale, first, count: questions.length });
    first += questions.length;
    for (const question of questions) {
      const size = RECORD_SIZE + question.idRaw.length + question.stemRaw.length +
        question.optionRaws.reduce((total, raw) => total + raw.length, 0);
      const record = new Uint8Array(size);
      const view = new DataView(record.buffer);
      view.setUint8(0, question.options.length);
      view.setUint8(1, question.answer);
      view.setUint16(2, question.idRaw.length, true);
      view.setUint16(4, question.stemRaw.length, true);
      for (let option = 0; option < 4; option++)
        view.setUint16(6 + option * 2, question.optionRaws[option]?.length || 0, true);
      let cursor = RECORD_SIZE;
      for (const raw of [question.idRaw, question.stemRaw, ...question.optionRaws]) {
        record.set(raw, cursor);
        cursor += raw.length;
      }
      indexRows.push({
        hash: crc32(encoder.encode(`${question.locale}\0${question.id}`)),
        offset: dataOffset,
        size: record.length,
      });
      records.push(record);
      dataOffset += record.length;
    }
  }

  const locales = new Uint8Array(localeRows.length * 24);
  const localeView = new DataView(locales.buffer);
  localeRows.forEach((row, index) => {
    locales.set(encoder.encode(row.locale), index * 24);
    localeView.setUint32(index * 24 + 16, row.first, true);
    localeView.setUint32(index * 24 + 20, row.count, true);
  });
  const indexes = new Uint8Array(indexRows.length * 12);
  const indexView = new DataView(indexes.buffer);
  indexRows.forEach((row, index) => {
    indexView.setUint32(index * 12, row.hash, true);
    indexView.setUint32(index * 12 + 4, row.offset, true);
    indexView.setUint32(index * 12 + 8, row.size, true);
  });
  const data = concatenate(records);
  const sections = [
    { tag: 'QLOC', data: locales, count: localeRows.length },
    { tag: 'QIDX', data: indexes, count: indexRows.length },
    { tag: 'QDAT', data, count: indexRows.length },
  ];
  const headerSize = COMMON_SIZE + SECTION_SIZE * sections.length;
  let payloadSize = 0;
  for (const section of sections) {
    payloadSize = (payloadSize + 3) & ~3;
    section.offset = headerSize + payloadSize;
    payloadSize += section.data.length;
  }
  const result = new Uint8Array(headerSize + payloadSize);
  const view = new DataView(result.buffer);
  fourcc(view, 0, 'TPPK');
  view.setUint16(4, PACK_ABI, true);
  view.setUint8(6, PACK_KIND_QUIZ);
  view.setUint8(7, 0);
  view.setUint32(8, result.length, true);
  view.setUint32(16, bank.revision, true);
  view.setUint32(20, 0, true);
  view.setUint16(24, headerSize, true);
  view.setUint16(26, sections.length, true);
  result.set(encoder.encode(bank.id), 28);
  sections.forEach((section, index) => {
    const offset = COMMON_SIZE + index * SECTION_SIZE;
    fourcc(view, offset, section.tag);
    view.setUint32(offset + 4, section.offset, true);
    view.setUint32(offset + 8, section.data.length, true);
    view.setUint32(offset + 12, section.count, true);
    result.set(section.data, section.offset);
  });
  view.setUint32(12, crc32(result.subarray(headerSize)), true);
  return result;
}

function ascii(raw) {
  const end = raw.indexOf(0);
  return decoder.decode(end < 0 ? raw : raw.subarray(0, end));
}

function sectionTag(raw, offset) {
  return String.fromCharCode(raw[offset], raw[offset + 1], raw[offset + 2], raw[offset + 3]);
}

export function readQuestionPack(source) {
  const raw = source instanceof Uint8Array ? source : new Uint8Array(source);
  if (raw.length < COMMON_SIZE) throw new Error('题库包已截断');
  const view = new DataView(raw.buffer, raw.byteOffset, raw.byteLength);
  const sectionCount = view.getUint16(26, true);
  const headerSize = view.getUint16(24, true);
  if (sectionTag(raw, 0) !== 'TPPK' || view.getUint16(4, true) !== PACK_ABI ||
      view.getUint8(6) !== PACK_KIND_QUIZ || view.getUint32(8, true) !== raw.length)
    throw new Error('不是兼容的 TamaPoke 题库包');
  if (headerSize !== COMMON_SIZE + sectionCount * SECTION_SIZE || headerSize > raw.length)
    throw new Error('题库包目录无效');
  if (view.getUint32(12, true) !== crc32(raw.subarray(headerSize)))
    throw new Error('题库包校验失败');
  const sections = new Map();
  let previousEnd = headerSize;
  for (let index = 0; index < sectionCount; index++) {
    const cursor = COMMON_SIZE + index * SECTION_SIZE;
    const tag = sectionTag(raw, cursor);
    const offset = view.getUint32(cursor + 4, true);
    const size = view.getUint32(cursor + 8, true);
    const count = view.getUint32(cursor + 12, true);
    if (sections.has(tag) || offset < previousEnd || offset > raw.length || size > raw.length - offset)
      throw new Error('题库包段目录无效');
    sections.set(tag, { data: raw.subarray(offset, offset + size), count });
    previousEnd = offset + size;
  }
  if (sections.size !== 3 || !sections.has('QLOC') || !sections.has('QIDX') || !sections.has('QDAT'))
    throw new Error('题库包缺少索引段');
  const localeSection = sections.get('QLOC');
  const indexSection = sections.get('QIDX');
  const dataSection = sections.get('QDAT');
  if (localeSection.data.length !== localeSection.count * 24 ||
      indexSection.data.length !== indexSection.count * 12 || dataSection.count !== indexSection.count)
    throw new Error('题库包索引大小无效');

  const locales = [];
  const localeView = new DataView(localeSection.data.buffer, localeSection.data.byteOffset,
    localeSection.data.byteLength);
  let covered = 0;
  for (let index = 0; index < localeSection.count; index++) {
    const cursor = index * 24;
    const locale = ascii(localeSection.data.subarray(cursor, cursor + 16));
    const first = localeView.getUint32(cursor + 16, true);
    const count = localeView.getUint32(cursor + 20, true);
    if (!LOCALE.test(locale) || first !== covered || first + count > indexSection.count)
      throw new Error('题库包语言索引无效');
    for (let item = 0; item < count; item++) locales.push(locale);
    covered += count;
  }
  if (covered !== indexSection.count) throw new Error('题库包索引没有完整覆盖题目');

  const questions = [];
  const indexes = new DataView(indexSection.data.buffer, indexSection.data.byteOffset,
    indexSection.data.byteLength);
  for (let number = 0; number < indexSection.count; number++) {
    const start = indexes.getUint32(number * 12 + 4, true);
    const size = indexes.getUint32(number * 12 + 8, true);
    if (size < RECORD_SIZE || start > dataSection.data.length || size > dataSection.data.length - start)
      throw new Error('题目记录超出数据段');
    const record = dataSection.data.subarray(start, start + size);
    const recordView = new DataView(record.buffer, record.byteOffset, record.byteLength);
    const optionCount = recordView.getUint8(0);
    const answer = recordView.getUint8(1);
    const sizes = [recordView.getUint16(2, true), recordView.getUint16(4, true)];
    for (let option = 0; option < 4; option++) sizes.push(recordView.getUint16(6 + option * 2, true));
    if (optionCount < 2 || optionCount > 4 || answer >= optionCount ||
        !sizes[0] || sizes[0] > 40 || !sizes[1] || sizes[1] > 768 ||
        sizes.slice(2, 2 + optionCount).some(length => !length || length > 192) ||
        sizes.slice(2 + optionCount).some(Boolean) || RECORD_SIZE + sizes.reduce((a, b) => a + b, 0) !== size)
      throw new Error('题目记录无效');
    if (record.subarray(RECORD_SIZE).includes(0)) throw new Error('题目记录不能包含 NUL');
    let cursor = RECORD_SIZE;
    const take = length => {
      const value = decoder.decode(record.subarray(cursor, cursor + length));
      cursor += length;
      return value;
    };
    const id = take(sizes[0]);
    const stem = take(sizes[1]);
    const options = sizes.slice(2, 2 + optionCount).map(take);
    questions.push({ id, locale: locales[number], stem, options, answer });
  }
  const id = ascii(raw.subarray(28, 48));
  return { schema: 1, id, label: id, revision: view.getUint32(16, true), questions };
}
