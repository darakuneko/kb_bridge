// node tools/test/compare.mjs <dfu.zip> <ref.bin>: our packets must match adafruit-nrfutil's byte for byte.
import { readFileSync } from 'node:fs';
import { readDfuPackage } from '../../docs/js/zip.js';
import { dfuPackets } from '../../docs/js/dfu.js';

const zip = readFileSync(process.argv[2]);
const { bin, dat } = await readDfuPackage(zip.buffer.slice(zip.byteOffset, zip.byteOffset + zip.byteLength));
const steps = dfuPackets(bin, dat);
const ours = Buffer.concat(steps.map((s) => Buffer.from(s.pkt)));
const ref = readFileSync(process.argv[3]);
console.log(steps.length, 'packets,', ours.length, 'bytes; reference', ref.length, 'bytes');
console.log(ours.equals(ref) ? 'MATCH' : 'DIFFERENT');
process.exit(ours.equals(ref) ? 0 : 1);
