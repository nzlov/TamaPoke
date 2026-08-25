import { SerialClient } from './serial-client.js';
import { buildQuestionPack, normalizeQuestionBank, readQuestionPack } from './question-pack.js';
import { createPackId, createQuestionId, paginateQuestions } from './question-bank-model.mjs';

const byId = id => document.getElementById(id);
const status = byId('status');
const questionList = byId('question-list');
const optionInputs = [...document.querySelectorAll('[data-option]')];
const answerInputs = [...document.querySelectorAll('input[name=answer]')];
let questions = [];
let editingIndex = -1;
let bankId = createPackId();
let bankRevision = 1;
let currentPage = 1;
let serial = null;
let deviceBusy = false;
const PAGE_SIZE = 10;
const QUESTION_TYPE_CHOICE = 1;
const QUESTION_TYPE_ARITHMETIC = 2;

function report(message, kind = 'info') {
  status.textContent = message;
  status.dataset.kind = kind;
}

function sourceDocument(requireQuestions = true) {
  const document = {
    schema: 1,
    id: bankId,
    label: bankId,
    revision: bankRevision,
    questions: questions.map(question => ({ ...question, options: [...question.options] })),
  };
  if (requireQuestions) normalizeQuestionBank(document);
  return document;
}

function adopt(document) {
  const normalized = normalizeQuestionBank(document);
  bankId = normalized.id;
  bankRevision = normalized.revision;
  questions = normalized.questions.map(({ id, locale, stem, options, answer }) =>
    ({ id, locale, stem, options: [...options], answer }));
  byId('question-search').value = '';
  currentPage = 1;
  clearEditor();
  renderQuestions();
}

function clearEditor() {
  editingIndex = -1;
  byId('question-form').reset();
  byId('question-locale').value = 'zh-CN';
  answerInputs[0].checked = true;
  byId('cancel-edit').hidden = true;
  byId('save-question-label').textContent = '添加题目';
}

function renderQuestions() {
  byId('question-count').textContent = String(questions.length);
  const result = paginateQuestions(questions, byId('question-search').value, currentPage, PAGE_SIZE);
  currentPage = result.page;
  byId('list-summary').textContent = byId('question-search').value.trim()
    ? `找到 ${result.totalItems} 道题`
    : `共 ${result.totalItems} 道题`;
  byId('page-info').textContent = `第 ${result.page} / ${result.totalPages} 页`;
  byId('previous-page').disabled = result.page <= 1;
  byId('next-page').disabled = result.page >= result.totalPages;
  questionList.textContent = '';
  if (!result.totalItems) {
    const empty = document.createElement('div');
    empty.className = 'empty';
    empty.textContent = questions.length
      ? '没有题干匹配当前搜索。'
      : '还没有题目。先在上方添加一道选择题。';
    questionList.append(empty);
    return;
  }
  result.items.forEach(({ question, index }) => {
    const row = document.createElement('article');
    row.className = 'question-row';
    const copy = document.createElement('div');
    const title = document.createElement('strong');
    title.textContent = question.stem;
    const detail = document.createElement('small');
    detail.textContent = `${question.locale} · ${question.options.length} 个选项 · 答案 ${String.fromCharCode(65 + question.answer)}`;
    copy.append(title, detail);
    const actions = document.createElement('div');
    actions.className = 'actions';
    const edit = document.createElement('button');
    edit.type = 'button';
    edit.textContent = '编辑';
    edit.onclick = () => editQuestion(index);
    const remove = document.createElement('button');
    remove.type = 'button';
    remove.className = 'danger';
    remove.textContent = '删除';
    remove.onclick = () => deleteQuestion(index);
    actions.append(edit, remove);
    row.append(copy, actions);
    questionList.append(row);
  });
}

function editQuestion(index) {
  const question = questions[index];
  editingIndex = index;
  byId('question-locale').value = question.locale;
  byId('question-stem').value = question.stem;
  optionInputs.forEach((input, option) => { input.value = question.options[option] || ''; });
  answerInputs[question.answer].checked = true;
  byId('cancel-edit').hidden = false;
  byId('save-question-label').textContent = '保存题目';
  byId('editor-heading').scrollIntoView({ behavior: 'smooth', block: 'start' });
}

function deleteQuestion(index) {
  const stem = questions[index].stem;
  if (!confirm(`删除题目“${stem.length > 28 ? `${stem.slice(0, 28)}…` : stem}”？`)) return;
  questions.splice(index, 1);
  if (editingIndex === index) clearEditor();
  else if (editingIndex > index) editingIndex--;
  renderQuestions();
  report('题目已删除。', 'success');
}

byId('question-form').onsubmit = event => {
  event.preventDefault();
  try {
    const rawOptions = optionInputs.map(input => input.value.trim());
    const firstEmpty = rawOptions.findIndex(value => !value);
    const optionCount = firstEmpty < 0 ? rawOptions.length : firstEmpty;
    if (optionCount < 2 || rawOptions.slice(optionCount).some(Boolean))
      throw new Error('选项必须从 A 开始连续填写，且至少填写 A、B');
    const answer = Number(answerInputs.find(input => input.checked)?.value ?? -1);
    if (answer >= optionCount) throw new Error('正确答案必须指向已填写的选项');
    const question = {
      locale: byId('question-locale').value.trim(),
      stem: byId('question-stem').value.trim(),
      options: rawOptions.slice(0, optionCount),
      answer,
    };
    question.id = editingIndex < 0
      ? createQuestionId(question, questions.map(item => item.id))
      : questions[editingIndex].id;
    const candidate = [...questions];
    if (editingIndex < 0) candidate.push(question); else candidate[editingIndex] = question;
    normalizeQuestionBank({ ...sourceDocument(false), questions: candidate });
    const wasEditing = editingIndex >= 0;
    questions = candidate;
    if (!wasEditing) {
      byId('question-search').value = '';
      currentPage = Math.ceil(questions.length / PAGE_SIZE);
    }
    clearEditor();
    renderQuestions();
    report(wasEditing ? '题目已保存。' : '题目已添加。', 'success');
  } catch (error) {
    report(error.message, 'error');
  }
};

byId('cancel-edit').onclick = () => { clearEditor(); report('已取消编辑。'); };
byId('question-search').oninput = () => { currentPage = 1; renderQuestions(); };
byId('previous-page').onclick = () => { currentPage--; renderQuestions(); };
byId('next-page').onclick = () => { currentPage++; renderQuestions(); };

function download(name, data, type) {
  const blob = new Blob([data], { type });
  const link = document.createElement('a');
  link.href = URL.createObjectURL(blob);
  link.download = name;
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(link.href);
}

async function importFile(input, reader) {
  const file = input.files?.[0];
  input.value = '';
  if (!file) return;
  try {
    adopt(await reader(file));
    report(`已导入 ${file.name}，共 ${questions.length} 道题。`, 'success');
  } catch (error) {
    report(`导入失败：${error.message}`, 'error');
  }
}

byId('import-json').onchange = event => importFile(event.target,
  async file => JSON.parse(await file.text()));
byId('import-pack').onchange = event => importFile(event.target,
  async file => readQuestionPack(await file.arrayBuffer()));

byId('export-json').onclick = () => {
  try {
    const document = sourceDocument();
    download(`${document.id}.json`, JSON.stringify(document, null, 2) + '\n', 'application/json');
    report('题库 JSON 已导出。', 'success');
  } catch (error) { report(error.message, 'error'); }
};

byId('export-pack').onclick = () => {
  try {
    const document = sourceDocument();
    download(`${document.id}.tquiz`, buildQuestionPack(document), 'application/octet-stream');
    report('索引题库包已构建并导出。', 'success');
  } catch (error) { report(error.message, 'error'); }
};

function boardError(line) {
  const code = line.slice(3).trim();
  const messages = {
    QUIZ_CONFIG_INVALID: '设备拒绝了答题规则，请检查各字段范围',
    SD_NOT_READY: 'microSD 未插入或挂载失败',
    PACK_VALIDATION_FAILED: '题库包校验失败',
    WRITE_FAILED: 'microSD 写入失败',
    READ_TIMEOUT: '传输超时',
    DOWNLOAD_CANCELLED: '设备下载握手失败',
    PACK_NOT_FOUND: '设备中的题库已不存在',
    PACK_READ_FAILED: 'microSD 读取题库失败',
  };
  return new Error(`${messages[code] || `设备错误：${code || '未知错误'}`} (${code || 'ERR'})`);
}

function setConnected(connected) {
  byId('connect').disabled = connected || deviceBusy;
  byId('deploy-pack').disabled = !connected || deviceBusy;
  byId('device-pack').disabled = !connected || deviceBusy || !byId('device-pack').value;
  byId('import-device').disabled = !connected || deviceBusy || !byId('device-pack').value;
  byId('read-config').disabled = !connected || deviceBusy;
  byId('write-config').disabled = !connected || deviceBusy;
  byId('device-state').textContent = connected ? '设备已连接' : '尚未连接设备';
}

async function refreshDeviceBanks(preferredPath = '') {
  const select = byId('device-pack');
  const lines = await serial.command('LS');
  const banks = lines.map(line => line.match(/^(\/packs\/[^/\s]+\.tquiz) (\d+)$/))
    .filter(Boolean)
    .map(match => ({ path: match[1], size: Number(match[2]) }));
  select.textContent = '';
  if (!banks.length) {
    const option = document.createElement('option');
    option.textContent = '设备中没有题库包';
    option.value = '';
    select.append(option);
  } else {
    banks.forEach(bank => {
      const option = document.createElement('option');
      option.value = bank.path;
      option.textContent = `${bank.path.slice(7)} · ${Math.ceil(bank.size / 1024)} KB`;
      select.append(option);
    });
    if (banks.some(bank => bank.path === preferredPath)) select.value = preferredPath;
  }
  setConnected(true);
}

function setDeviceBusy(busy) {
  deviceBusy = busy;
  setConnected(Boolean(serial?.connected));
}

if (!('serial' in navigator)) {
  byId('connect').disabled = true;
  byId('device-state').textContent = '当前浏览器不支持 Web Serial，请使用桌面版 Chrome 或 Edge';
}

byId('connect').onclick = async () => {
  try {
    setDeviceBusy(true);
    serial = new SerialClient(boardError, error => report(`串口读取停止：${error.message}`, 'error'));
    await serial.connect();
    await refreshDeviceBanks();
    report('设备已连接，可以导入、部署题库或读取答题规则。', 'success');
  } catch (error) { report(`连接失败：${error.message}`, 'error'); }
  finally { setDeviceBusy(false); }
};

byId('device-pack').onchange = () => setConnected(Boolean(serial?.connected));

byId('import-device').onclick = async () => {
  const path = byId('device-pack').value;
  const progress = byId('deploy-progress');
  if (!path) return;
  try {
    setDeviceBusy(true);
    progress.hidden = false;
    progress.value = 0;
    report(`正在从设备读取 ${path.slice(7)}…`);
    const raw = await serial.readFile(path,
      (received, total) => { progress.value = Math.floor(received / total * 100); });
    progress.value = 100;
    adopt(readQuestionPack(raw));
    report(`已从设备导入 ${path.slice(7)}，共 ${questions.length} 道题。`, 'success');
  } catch (error) {
    report(`设备导入失败：${error.message}`, 'error');
  } finally {
    setDeviceBusy(false);
  }
};

byId('deploy-pack').onclick = async () => {
  const progress = byId('deploy-progress');
  try {
    const document = sourceDocument();
    const raw = buildQuestionPack(document);
    setDeviceBusy(true);
    progress.hidden = false;
    progress.value = 0;
    report(`正在部署 ${document.id}.tquiz…`);
    await serial.sendFile(`packs/${document.id}.tquiz`, raw,
      sent => { progress.value = Math.floor(sent / raw.length * 100); });
    progress.value = 100;
    await refreshDeviceBanks(`/packs/${document.id}.tquiz`);
    report(`题库已部署，共 ${document.questions.length} 道题；重启设备后生效。`, 'success');
  } catch (error) {
    report(`部署失败：${error.message}`, 'error');
  } finally {
    setDeviceBusy(false);
  }
};

function numeric(id) { return Number(byId(id).value); }

function updateConfigAvailability() {
  const choiceEnabled = byId('enable-choice').checked;
  const arithmeticEnabled = byId('enable-arithmetic').checked;
  const anyEnabled = choiceEnabled || arithmeticEnabled;
  byId('time-seconds').disabled = !anyEnabled;
  byId('time-seconds-field').classList.toggle('setting-disabled', !anyEnabled);
  byId('choice-weight').disabled = !(choiceEnabled && arithmeticEnabled);
  byId('choice-weight-field').classList.toggle('setting-disabled',
    !(choiceEnabled && arithmeticEnabled));
  document.querySelectorAll('[data-arithmetic-setting]').forEach(input => {
    input.disabled = !arithmeticEnabled;
  });
  document.querySelectorAll('[data-arithmetic-container]').forEach(container => {
    container.classList.toggle('setting-disabled', !arithmeticEnabled);
    container.setAttribute('aria-disabled', String(!arithmeticEnabled));
  });
}

function configValues() {
  const operators = [...document.querySelectorAll('[data-operator]:checked')]
    .reduce((mask, input) => mask | Number(input.dataset.operator), 0);
  const flags = (byId('allow-negative').checked ? 1 : 0) |
    (byId('allow-decimals').checked ? 2 : 0) |
    (byId('allow-fractions').checked ? 4 : 0) |
    (byId('allow-parentheses').checked ? 8 : 0) |
    (byId('division-exact').checked ? 16 : 0);
  const questionTypes = (byId('enable-choice').checked ? QUESTION_TYPE_CHOICE : 0) |
    (byId('enable-arithmetic').checked ? QUESTION_TYPE_ARITHMETIC : 0);
  const values = [numeric('time-seconds'), operators, numeric('operand-count'),
    numeric('operand-digits'), numeric('answer-digits'), numeric('decimal-places'),
    numeric('fraction-digits'), flags, numeric('parenthesis-depth'), numeric('choice-weight'),
    questionTypes];
  if (!Number.isInteger(values[0]) || values[0] < 5 || values[0] > 120 ||
      ((questionTypes & QUESTION_TYPE_ARITHMETIC) && !operators) ||
      values[2] < 2 || values[2] > 4 || values[3] < 1 || values[3] > 3 ||
      values[4] < 1 || values[4] > 6 || values[5] < 1 || values[5] > 3 ||
      values[6] < 1 || values[6] > 3 || values[8] < 0 || values[8] > 3 ||
      ((flags & 8) && values[8] < 1) ||
      values[9] < 0 || values[9] > 100 || values.some(value => !Number.isInteger(value)))
    throw new Error('答题规则超出允许范围；开启四则运算时至少启用一个运算符');
  return values;
}

function showConfig(values) {
  if (values.length !== 11 || values.some(value => !Number.isInteger(value)))
    throw new Error('设备返回了无效的答题规则');
  const ids = ['time-seconds', null, 'operand-count', 'operand-digits', 'answer-digits',
    'decimal-places', 'fraction-digits', null, 'parenthesis-depth', 'choice-weight'];
  ids.forEach((id, index) => { if (id) byId(id).value = values[index]; });
  document.querySelectorAll('[data-operator]').forEach(input => {
    input.checked = Boolean(values[1] & Number(input.dataset.operator));
  });
  byId('allow-negative').checked = Boolean(values[7] & 1);
  byId('allow-decimals').checked = Boolean(values[7] & 2);
  byId('allow-fractions').checked = Boolean(values[7] & 4);
  byId('allow-parentheses').checked = Boolean(values[7] & 8);
  byId('division-exact').checked = Boolean(values[7] & 16);
  byId('enable-choice').checked = Boolean(values[10] & QUESTION_TYPE_CHOICE);
  byId('enable-arithmetic').checked = Boolean(values[10] & QUESTION_TYPE_ARITHMETIC);
  updateConfigAvailability();
}

function configFromLines(lines) {
  const row = lines.find(line => line.startsWith('QUIZCFG\t'));
  if (!row) throw new Error('设备没有返回答题规则');
  const values = row.split('\t').slice(1).map(Number);
  showConfig(values);
}

byId('read-config').onclick = async () => {
  try {
    setDeviceBusy(true);
    configFromLines(await serial.command('QUIZCFG'));
    report('已读取设备的全局答题规则。', 'success');
  } catch (error) { report(`读取失败：${error.message}`, 'error'); }
  finally { setDeviceBusy(false); }
};

byId('write-config').onclick = async () => {
  try {
    setDeviceBusy(true);
    const values = configValues();
    configFromLines(await serial.command(`QUIZSET ${values.join(' ')}`));
    report('全局答题规则已保存到设备。', 'success');
  } catch (error) { report(`保存失败：${error.message}`, 'error'); }
  finally { setDeviceBusy(false); }
};

byId('enable-choice').onchange = updateConfigAvailability;
byId('enable-arithmetic').onchange = updateConfigAvailability;
updateConfigAvailability();
renderQuestions();
