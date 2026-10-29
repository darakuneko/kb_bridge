import { flash } from './dfu.js';
import { readDfuPackage } from './zip.js';

// KB Bridge update mode: the CH559 shows up as this CDC serial port (fw/ch559/cdc.c).
// KB Bridge update-mode serial
const USB_FILTERS = [{ usbVendorId: 0x7A79, usbProductId: 0x1576 }];

const TEXT = {
  ja: {
    lead: 'KB Bridge のファームウェアを、ブラウザから USB で書き込みます。ソフトのインストールは要りません。',
    unsupported: 'このブラウザは Web Serial に対応していません。パソコンの Chrome か Edge で開いてください（Safari・Firefox・スマートフォンでは使えません）。',
    s1: '1. ファームウェアを選ぶ',
    board: '基板',
    version: 'バージョン',
    localSummary: '手元のファイルを使う（上級者向け）',
    localHelp: 'kb_bridge_dfu.zip を選ぶと、上の一覧の代わりにそのファイルを書き込みます。',
    s2: '2. 書き込む',
    step1: 'KB Bridge の PAIR ボタンを押したまま、電源用の USB-C をパソコンにつなぐ（つないだらボタンは離してよい）',
    step2: '下の「書き込む」を押し、出てきた一覧から「KB Bridge Update」を選んで「接続」',
    step3: '「完了」と出るまでケーブルを抜かない（30 秒ほど）',
    go: '書き込む',
    s3: '3. 進み具合',
    erase: '準備中（消去しています）…',
    write: '書き込み中… {p}%',
    activate: '仕上げ中…',
    done: '完了しました。\nUSB を抜いて、PAIR を押さずにつなぎ直してください。最初の起動で数秒、赤と緑が交互に点滅することがあります（内部の USB チップの更新）。そのまま待ってください。',
    NO_BOOTLOADER: 'KB Bridge から応答がありません。\nいったん抜いて、PAIR を押したままつなぎ直してから、もう一度「書き込む」を押してください。',
    NO_ACK: '書き込みの途中で応答が止まりました。\n抜いて、PAIR を押したままつなぎ直し、もう一度書き込んでください。途中で止まっても、書き直せば元に戻ります。\n何回か試してもうまくいかないときは、GitHub のリポジトリを clone して、手元で書き込んでください（README の「Local testing」）。',
    BAD_PACKAGE: 'ファームウェアのファイルが壊れているか、形式が違います。',
    FETCH: 'ファームウェアを読み込めませんでした。ページを再読み込みしてください。',
    OPEN: 'ポートを開けませんでした。ほかのアプリ（Arduino IDE、シリアルモニターなど）が使っていないか確かめてください。',
    noFirmware: '（公開中のファームウェアはありません）',
    latest: '（最新）',
    footer: 'Chrome / Edge（Windows・macOS・Linux）で動きます。書き込みはすべてこのブラウザの中で行われ、ファイルや情報が外部に送られることはありません。',
    guideLink: 'ビルドガイド（はんだ付け・組み立て・使い方）→',
    ch559Link: '組み立ての最初に: CH559 の書き込み（HY0020 を付ける前に 1 回）→',
  },
  en: {
    lead: 'Write KB Bridge firmware from your browser over USB. Nothing to install.',
    unsupported: 'This browser does not support Web Serial. Open this page in Chrome or Edge on a computer (Safari, Firefox and phones do not work).',
    s1: '1. Choose the firmware',
    board: 'Board',
    version: 'Version',
    localSummary: 'Use a local file (advanced)',
    localHelp: 'Pick a kb_bridge_dfu.zip to write it instead of the list above.',
    s2: '2. Write it',
    step1: 'Hold the PAIR button on the KB Bridge and plug its power USB-C into the computer (you can let go once it is plugged in)',
    step2: 'Press "Write" below, choose "KB Bridge Update" in the list and press "Connect"',
    step3: 'Keep the cable plugged in until it says "Done" (about 30 s)',
    go: 'Write',
    s3: '3. Progress',
    erase: 'Preparing (erasing)…',
    write: 'Writing… {p}%',
    activate: 'Finishing…',
    done: 'Done.\nUnplug the USB cable and plug it in again without holding PAIR. On the first start the LED may alternate red and green for a few seconds (the internal USB chip is updated); just wait.',
    NO_BOOTLOADER: 'No answer from the KB Bridge.\nUnplug it, plug it in again while holding PAIR, then press "Write" again.',
    NO_ACK: 'The KB Bridge stopped answering during the write.\nUnplug it, plug it in again while holding PAIR and write again. An interrupted write is fixed by writing again.\nIf it keeps failing, clone the GitHub repository and write from your own computer (see "Local testing" in the README).',
    BAD_PACKAGE: 'The firmware file is broken or not a KB Bridge DFU package.',
    FETCH: 'Could not load the firmware. Please reload the page.',
    OPEN: 'Could not open the port. Make sure no other app (Arduino IDE, a serial monitor, …) is using it.',
    noFirmware: '(no firmware published)',
    latest: ' (latest)',
    footer: 'Works in Chrome / Edge on Windows, macOS and Linux. Everything happens inside this browser; no files or data are sent anywhere.',
    guideLink: 'Build guide (soldering, assembly, usage) →',
    ch559Link: 'First step of the build: write the CH559 (once, before fitting the HY0020) →',
  },
};

const $ = (id) => document.getElementById(id);
function savedLang() {
  try { return localStorage.getItem('lang'); } catch (e) { return null; }
}
let lang = (savedLang() || navigator.language || 'ja').startsWith('ja') ? 'ja' : 'en';
let index = { boards: [] };
let localFile = null;
let busy = false;

const t = (key) => TEXT[lang][key] || key;

function applyText() {
  document.documentElement.lang = lang;
  for (const el of document.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
  $('lang').textContent = lang === 'ja' ? 'English' : '日本語';
  $('guide').href = lang === 'ja' ? 'guide_ja.html' : 'guide_en.html';
  fillBoards();
}

function fillBoards() {
  const keep = $('board').value;
  $('board').innerHTML = '';
  for (const b of index.boards) $('board').add(new Option(b.name, b.id));
  if (keep) $('board').value = keep;
  fillVersions();
}

function currentBoard() {
  return index.boards.find((b) => b.id === $('board').value);
}

function fillVersions() {
  const b = currentBoard();
  $('fw').innerHTML = '';
  if (!b || !b.firmware.length) {
    $('fw').add(new Option(t('noFirmware'), ''));
    $('notes').textContent = '';
    return;
  }
  b.firmware.forEach((f, i) => $('fw').add(new Option(`${f.version} — ${f.date}${i === 0 ? t('latest') : ''}`, f.file)));
  showNotes();
}

function showNotes() {
  const b = currentBoard();
  const f = b && b.firmware.find((x) => x.file === $('fw').value);
  $('notes').textContent = f ? (f.notes && (f.notes[lang] || f.notes.ja)) || '' : '';
}

function setStatus(text, cls = '') {
  $('status').textContent = text;
  $('status').className = 'status ' + cls;
}

function setBar(frac) {
  $('barfill').style.width = `${Math.round(frac * 100)}%`;
}

async function loadPackage() {
  if (localFile) return readDfuPackage(await localFile.arrayBuffer());
  const file = $('fw').value;
  if (!file) throw new Error('FETCH');
  let res;
  try {
    res = await fetch(file, { cache: 'no-cache' });
  } catch (e) {
    throw new Error('FETCH');
  }
  if (!res.ok) throw new Error('FETCH');
  return readDfuPackage(await res.arrayBuffer());
}

async function run() {
  if (busy) return;
  let port;
  try {
    port = await navigator.serial.requestPort({ filters: USB_FILTERS });
  } catch (e) {
    return;                                   // the user closed the chooser
  }
  busy = true;
  $('go').disabled = true;
  $('run').hidden = false;
  setBar(0);
  setStatus('');
  try {
    let pkg;
    try {
      pkg = await loadPackage();
    } catch (e) {
      throw new Error(e.message === 'FETCH' ? 'FETCH' : 'BAD_PACKAGE');
    }
    await flash(port, pkg.bin, pkg.dat, (done, total, phase) => {
      setBar(done / total);
      setStatus(t(phase).replace('{p}', Math.floor((100 * done) / total)));
    });
    setBar(1);
    setStatus(t('done'), 'ok');
  } catch (e) {
    const known = ['NO_BOOTLOADER', 'NO_ACK', 'BAD_PACKAGE', 'FETCH'];
    const key = known.includes(e.message) ? e.message : e.name === 'NetworkError' || e.name === 'InvalidStateError' ? 'OPEN' : null;
    setStatus(key ? t(key) : `${t('NO_ACK')}\n(${e.name}: ${e.message})`, 'error');
  } finally {
    busy = false;
    $('go').disabled = false;
  }
}

async function init() {
  if (!('serial' in navigator)) {
    $('unsupported').hidden = false;
    $('go').disabled = true;
  }
  try {
    const res = await fetch('firmware/index.json', { cache: 'no-cache' });
    index = await res.json();
  } catch (e) {
    index = { boards: [] };
  }
  applyText();
  $('lang').addEventListener('click', () => {
    lang = lang === 'ja' ? 'en' : 'ja';
    try { localStorage.setItem('lang', lang); } catch (e) { /* private mode */ }
    applyText();
  });
  $('board').addEventListener('change', fillVersions);
  $('fw').addEventListener('change', showNotes);
  $('local').addEventListener('change', (e) => { localFile = e.target.files[0] || null; });
  $('go').addEventListener('click', run);
}

init();
