import { USB_FILTER, flashCh559 } from './wchisp.js';

const TEXT = {
  ja: {
    lead: '組み立ての最初に 1 回だけ、USB チップ（CH559）にファームウェアを書き込むページです。HY0020 をはんだ付けする前に行ってください。以後の更新は通常の更新ページで CH559 も自動で書き換わるので、ここは使いません。',
    back: '← 通常の更新ページ',
    guideLink: 'ビルドガイドの手順',
    unsupported: 'このブラウザは WebUSB に対応していません。パソコンの Chrome か Edge で開いてください。',
    s1: '1. ファームウェアを選ぶ',
    board: '基板',
    version: 'バージョン',
    localSummary: '手元のファイルを使う',
    localHelp: 'kb_bridge_ch559.bin を選ぶと、上の一覧の代わりにそのファイルを書き込みます。fw/build.sh でビルドしたものを使ってください。',
    s2: '2. 書き込む',
    step1: 'JP1（シルク「BOOT」）の 2 つの穴をジャンパー線の端子などでショートしたまま、電源用の USB-C をパソコンにつなぐ（CH559 が書き込み待ちで起動します）',
    step2: '下の「書き込む」を押し、一覧に出た WCH のデバイス（4348:55E0）を選んで「接続」',
    step3: '「完了」と出たら USB を抜き、JP1 のショートをやめる',
    windows: 'Windows では、最初の 1 回だけ Zadig で このデバイス（4348:55E0）に WinUSB ドライバーを入れておく必要があります。macOS と Linux（udev の設定済み）はそのまま使えます。',
    go: '書き込む',
    s3: '3. 進み具合',
    erase: '消去中…',
    write: '書き込み中… {p}%',
    verify: '確認中… {p}%',
    done: '完了しました。\nUSB を抜いて、JP1 のショートをやめてください。HY0020 を付けていなければ LED は光りません。動作を確かめるなら、PAIR を押しながらつなぎ、パソコンに「KB Bridge Update」（シリアルポート）が出れば OK です。',
    NO_RESPONSE: 'CH559 の書き込みモードから応答がありません。\nいったん抜いて、JP1 をショートしたままつなぎ直してから、もう一度お試しください。',
    WRONG_CHIP: 'CH559 ではないデバイスです。',
    ERASE: '消去に失敗しました。もう一度お試しください。',
    KEY: '書き込みの準備（暗号キー）に失敗しました。抜いてつなぎ直し、もう一度お試しください。',
    PROGRAM: '書き込みに失敗しました。抜いてつなぎ直し、もう一度お試しください。',
    VERIFY: '書いた内容の確認で違いが見つかりました。抜いてつなぎ直し、もう一度お試しください。',
    FETCH: 'ファームウェアを読み込めませんでした。ページを再読み込みしてください。',
    OPEN: 'デバイスを開けませんでした。Windows ではドライバー（WinUSB）を確かめてください。ほかのアプリ（wchisp、WCHISPTool など）が使っていないかも確かめてください。',
    noFirmware: '（公開中のファームウェアはありません）',
    latest: '（最新）',
  },
  en: {
    lead: 'Write the USB chip (CH559) firmware once, at the start of the build, before the HY0020 is soldered on. Later updates go through the normal update page, which updates the CH559 as well.',
    back: '← Normal update page',
    guideLink: 'Steps in the build guide',
    unsupported: 'This browser does not support WebUSB. Open this page in Chrome or Edge on a computer.',
    s1: '1. Choose the firmware',
    board: 'Board',
    version: 'Version',
    localSummary: 'Use a local file',
    localHelp: 'Pick a kb_bridge_ch559.bin to write it instead of the list above. Use a build made with fw/build.sh.',
    s2: '2. Write it',
    step1: 'Short the two JP1 holes (silkscreen "BOOT") (e.g. with the bare end of a jumper wire) and keep them shorted while you plug the power USB-C into the computer (the CH559 starts in its ISP mode)',
    step2: 'Press "Write" below, choose the WCH device (4348:55E0) and press "Connect"',
    step3: 'When it says "Done", unplug the USB cable and remove the JP1 short',
    windows: 'On Windows, install the WinUSB driver for this device (4348:55E0) with Zadig once. macOS and Linux (with a udev rule) work as they are.',
    go: 'Write',
    s3: '3. Progress',
    erase: 'Erasing…',
    write: 'Writing… {p}%',
    verify: 'Verifying… {p}%',
    done: 'Done.\nUnplug the USB cable and remove the JP1 short. Without the HY0020 fitted the LED stays dark. To check it, plug it in while holding PAIR: a "KB Bridge Update" serial port should appear on the computer.',
    NO_RESPONSE: 'No answer from the CH559 ISP mode.\nUnplug it, plug it in again with JP1 shorted, and try again.',
    WRONG_CHIP: 'This device is not a CH559.',
    ERASE: 'Erasing failed. Please try again.',
    KEY: 'Setting up the write (key) failed. Unplug, plug in again and retry.',
    PROGRAM: 'Writing failed. Unplug, plug in again and retry.',
    VERIFY: 'Verification found a difference. Unplug, plug in again and retry.',
    FETCH: 'Could not load the firmware. Please reload the page.',
    OPEN: 'Could not open the device. On Windows, check the WinUSB driver. Also make sure no other app (wchisp, WCHISPTool, …) is using it.',
    noFirmware: '(no firmware published)',
    latest: ' (latest)',
  },
};

const $ = (id) => document.getElementById(id);
function savedLang() {
  try { return localStorage.getItem('lang'); } catch (e) { return null; }
}
const urlLang = new URLSearchParams(location.search).get('lang');   // set by the build guide's links
let lang = (urlLang || savedLang() || navigator.language || 'ja').startsWith('ja') ? 'ja' : 'en';
let boards = [];
let localFile = null;
let busy = false;

const t = (key) => TEXT[lang][key] || key;

function applyText() {
  document.documentElement.lang = lang;
  for (const el of document.querySelectorAll('[data-i18n]')) el.textContent = t(el.dataset.i18n);
  $('lang').textContent = lang === 'ja' ? 'English' : '日本語';
  $('guide').href = lang === 'ja' ? 'guide_ja.html#ch559' : 'guide_en.html#ch559';
  $('back').href = `./?lang=${lang}`;
  fillBoards();
}

function fillBoards() {
  const keep = $('board').value;
  $('board').innerHTML = '';
  for (const b of boards) $('board').add(new Option(b.name, b.id));
  if (keep) $('board').value = keep;
  fillVersions();
}

function fillVersions() {
  const b = boards.find((x) => x.id === $('board').value);
  $('fw').innerHTML = '';
  if (!b || !b.firmware.length) {
    $('fw').add(new Option(t('noFirmware'), ''));
    return;
  }
  b.firmware.forEach((f, i) => $('fw').add(new Option(
    `${f.version} — ${f.date} (CH559 ${f.ch559.build_id})${i === 0 ? t('latest') : ''}`, f.ch559.file)));
}

function setStatus(text, cls = '') {
  $('status').textContent = text;
  $('status').className = 'status ' + cls;
}

async function loadImage() {
  if (localFile) return new Uint8Array(await localFile.arrayBuffer());
  const file = $('fw').value;
  if (!file) throw new Error('FETCH');
  const res = await fetch(file, { cache: 'no-cache' }).catch(() => null);
  if (!res || !res.ok) throw new Error('FETCH');
  return new Uint8Array(await res.arrayBuffer());
}

async function run() {
  if (busy) return;
  let dev;
  try {
    dev = await navigator.usb.requestDevice({ filters: [USB_FILTER] });
  } catch (e) {
    return;                                   // the user closed the chooser
  }
  busy = true;
  $('go').disabled = true;
  $('run').hidden = false;
  $('barfill').style.width = '0%';
  setStatus('');
  try {
    const image = await loadImage();
    await flashCh559(dev, image, (frac, phase) => {
      $('barfill').style.width = `${Math.round(frac * 100)}%`;
      setStatus(t(phase).replace('{p}', Math.floor(frac * 100)));
    });
    $('barfill').style.width = '100%';
    setStatus(t('done'), 'ok');
  } catch (e) {
    const known = ['NO_RESPONSE', 'WRONG_CHIP', 'ERASE', 'KEY', 'PROGRAM', 'VERIFY', 'FETCH'];
    const key = known.includes(e.message) ? e.message : e.name === 'SecurityError' || e.name === 'NetworkError' ? 'OPEN' : null;
    const detail = e.detail ? `\n(${e.detail})` : '';
    setStatus(key ? t(key) + detail : `${t('NO_RESPONSE')}\n(${e.name}: ${e.message})`, 'error');
  } finally {
    busy = false;
    $('go').disabled = false;
  }
}

async function init() {
  if (!('usb' in navigator)) {
    $('unsupported').hidden = false;
    $('go').disabled = true;
  }
  try {
    const index = await (await fetch('firmware/index.json', { cache: 'no-cache' })).json();
    boards = index.boards.map((b) => ({ ...b, firmware: b.firmware.filter((f) => f.ch559) }));
  } catch (e) {
    boards = [];
  }
  applyText();
  $('lang').addEventListener('click', () => {
    lang = lang === 'ja' ? 'en' : 'ja';
    try { localStorage.setItem('lang', lang); } catch (e) { /* private mode */ }
    if (urlLang) history.replaceState(null, '', `?lang=${lang}${location.hash}`);   // a reload keeps the choice
    applyText();
  });
  $('board').addEventListener('change', fillVersions);
  $('local').addEventListener('change', (e) => { localFile = e.target.files[0] || null; });
  $('go').addEventListener('click', run);
}

init();
