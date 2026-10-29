// CH559 first flash over WCH's USB ISP bootloader (what fw/tools/flash_ch559.sh does with wchisp), via WebUSB.
// Same command sequence as the nRF's serial ISP (fw/nrf/kb_bridge/ch559_isp.cpp), USB framing:
//   request : cmd, len_lo, len_hi, payload[len]        (bulk OUT endpoint 2)
//   response: cmd, status, len_lo, len_hi, payload[len] (bulk IN endpoint 2)

export const USB_FILTER = { vendorId: 0x4348, productId: 0x55e0 };

const CMD_IDENTIFY = 0xa1, CMD_ISP_END = 0xa2, CMD_ISP_KEY = 0xa3, CMD_ERASE = 0xa4,
  CMD_PROGRAM = 0xa5, CMD_VERIFY = 0xa6, CMD_READ_CONFIG = 0xa7;
const CHIP_ID = 0x59, CHIP_TYPE = 0x11;       // CH559, CH55x series
const CHUNK = 56;
const EP = 2;

class Isp {
  constructor(dev) { this.dev = dev; }

  async transfer(cmd, payload = [], timeoutMs = 1000) {
    const req = Uint8Array.from([cmd, payload.length & 0xff, payload.length >> 8, ...payload]);
    await this.dev.transferOut(EP, req);
    let timer;
    const timeout = new Promise((_, rej) => { timer = setTimeout(() => rej(new Error('NO_RESPONSE')), timeoutMs); });
    try {
      const res = await Promise.race([this.dev.transferIn(EP, 64), timeout]);
      const u8 = new Uint8Array(res.data.buffer, res.data.byteOffset, res.data.byteLength);
      if (u8.length < 4 || u8[0] !== cmd) throw new Error('NO_RESPONSE');
      const len = u8[2] | (u8[3] << 8);
      return u8.slice(4, 4 + len);
    } finally {
      clearTimeout(timer);
    }
  }

  // PROGRAM needs a final empty chunk (the bootloader writes its buffer only then); VERIFY stops at the end.
  async pass(cmd, image, key, onChunk) {
    // the empty chunk goes at addr = image length (not rounded up to a chunk): the bootloader rejects it otherwise
    for (let addr = 0, n; ; addr += n) {
      n = Math.min(CHUNK, image.length - addr);
      if (cmd === CMD_VERIFY && n === 0) break;
      const buf = [addr & 0xff, (addr >> 8) & 0xff, (addr >> 16) & 0xff, addr >>> 24, (Math.random() * 256) | 0];
      for (let i = 0; i < n; i++) buf.push(image[addr + i] ^ key[i & 7]);
      const r = await this.transfer(cmd, buf, 300);
      // Over USB the closing empty chunk is answered with 0xFE (bootloader V2.40);
      // VERIFY afterwards is what proves the flash.
      const bad = cmd === CMD_VERIFY ? r.length < 1 || r[0] !== 0 : n > 0 && r.length >= 1 && r[0] !== 0;
      if (bad) {
        const e = new Error(cmd === CMD_VERIFY ? 'VERIFY' : 'PROGRAM');
        e.detail = `addr 0x${addr.toString(16)}, resp [${[...r].map((x) => x.toString(16)).join(' ')}]`;
        throw e;
      }
      onChunk(Math.min(addr + n, image.length));
      if (n === 0) break;
    }
  }
}

// onProgress(fraction, phase): phase = 'erase' | 'write' | 'verify'
export async function flashCh559(dev, data, onProgress = () => {}) {
  // Over USB a last chunk that is not a multiple of 8 bytes is never written (VERIFY fails there):
  // pad with the erased value.
  const image = new Uint8Array(Math.ceil(data.length / 8) * 8).fill(0xff);
  image.set(data);
  await dev.open();
  try {
    if (dev.configuration === null) await dev.selectConfiguration(1);
    await dev.claimInterface(0);
    const isp = new Isp(dev);

    const id = [CHIP_ID, CHIP_TYPE, ...new TextEncoder().encode('MCU ISP & WCH.CN')];
    const who = await isp.transfer(CMD_IDENTIFY, id);
    if (who.length < 2 || who[0] !== CHIP_ID || who[1] !== CHIP_TYPE) throw new Error('WRONG_CHIP');

    // [mask, 0, config x12, bootloader version x4, UID ...]
    const cfg = await isp.transfer(CMD_READ_CONFIG, [0x1f, 0x00]);
    if (cfg.length < 22) throw new Error('NO_RESPONSE');
    const s = (cfg[18] + cfg[19] + cfg[20] + cfg[21]) & 0xff;
    const key = new Array(8).fill(s);
    key[7] = (key[7] + CHIP_ID) & 0xff;
    const keySum = key.reduce((a, b) => (a + b) & 0xff, 0);
    const seed = new Array(0x1e).fill(0);

    onProgress(0, 'erase');
    const sectors = Math.max(8, Math.ceil(image.length / 1024));
    const er = await isp.transfer(CMD_ERASE, [sectors & 0xff, sectors >> 8, 0, 0], 5000);
    if (er.length >= 1 && er[0] !== 0) throw new Error('ERASE');

    let k = await isp.transfer(CMD_ISP_KEY, seed);
    if (k.length < 1 || k[0] !== keySum) throw new Error('KEY');
    await isp.pass(CMD_PROGRAM, image, key, (d) => onProgress((0.8 * d) / image.length, 'write'));

    k = await isp.transfer(CMD_ISP_KEY, seed);
    if (k.length < 1 || k[0] !== keySum) throw new Error('KEY');
    await isp.pass(CMD_VERIFY, image, key, (d) => onProgress(0.8 + (0.2 * d) / image.length, 'verify'));

    try {
      await isp.transfer(CMD_ISP_END, [0], 300);   // the chip restarts into the application
    } catch (e) {
      // it may drop off the bus before answering
    }
  } finally {
    try { await dev.close(); } catch (e) { /* already gone after ISP_END */ }
  }
}
