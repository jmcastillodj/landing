// Genera variantes responsive (WebP) y favicons a partir de media-src/ usando Chromium (sin dependencias nativas).
// Uso: node scripts/images.mjs
import { chromium } from 'playwright-core';
import fs from 'node:fs';
import path from 'node:path';

const ROOT = path.resolve(import.meta.dirname, '..');
const SRC = path.join(ROOT, 'media-src');
const OUT = path.join(ROOT, 'src/assets/img');
fs.mkdirSync(path.join(OUT, 'covers'), { recursive: true });

/** [origen, nombre destino (sin extensión ni ancho), anchos] */
const jobs = [
  ['jm-castillo-estudio.jpg', 'jm-castillo-ingeniero-mezcla-mastering', [480, 760, 1000]],
  ['estudio-jm-castillo-sevilla.jpg', 'estudio-mezcla-mastering-jm-castillo-sevilla', [640, 1024, 1500]],
  ['sellos-discograficas.png', 'sellos-discograficas-warner-universal-sony', [650, 1300]],
  ['video-rueda-rvfv-david-bisbal.webp', 'video-rueda-rvfv-david-bisbal', [640, 1280]],
  ['video-desamarte-luis-cortes-camilo.webp', 'video-desamarte-luis-cortes-camilo', [640, 1200]],
  ...fs.readdirSync(path.join(SRC, 'covers')).map((f) => [
    'covers/' + f,
    'covers/' + f.replace(/\.(jpe?g|png|webp)$/, ''),
    f.startsWith('pantera') ? [300, 500] : [300, 640],
  ]),
];
const icons = [
  ['isotipo-jm-castillo.png', 'favicon-32.png', 32],
  ['isotipo-jm-castillo.png', 'apple-touch-icon.png', 180],
  ['isotipo-jm-castillo.png', 'icon-192.png', 192],
];

const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome' });
const page = await browser.newPage();
await page.setContent('<html><body></body></html>');

async function encode(file, width, type, quality) {
  const b64 = fs.readFileSync(path.join(SRC, file)).toString('base64');
  const mime = file.endsWith('.png') ? 'image/png' : file.endsWith('.webp') ? 'image/webp' : 'image/jpeg';
  return page.evaluate(async ({ b64, mime, width, type, quality }) => {
    const img = new Image();
    img.src = `data:${mime};base64,${b64}`;
    await img.decode();
    const w = Math.min(width, img.naturalWidth);
    const h = Math.round((img.naturalHeight * w) / img.naturalWidth);
    const c = document.createElement('canvas');
    c.width = w; c.height = h;
    const ctx = c.getContext('2d');
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(img, 0, 0, w, h);
    const url = c.toDataURL(type, quality);
    return { w, h, data: url.split(',')[1] };
  }, { b64, mime, width, type, quality });
}

const manifest = {};
for (const [file, name, widths] of jobs) {
  manifest[name] = [];
  for (const w of widths) {
    const r = await encode(file, w, 'image/webp', 0.8);
    const out = `${name}-${r.w}.webp`;
    fs.writeFileSync(path.join(OUT, out), Buffer.from(r.data, 'base64'));
    manifest[name].push({ file: out, w: r.w, h: r.h, kb: +(Buffer.byteLength(r.data, 'base64') / 1024).toFixed(1) });
  }
}
for (const [file, out, size] of icons) {
  const r = await encode(file, size, 'image/png');
  fs.writeFileSync(path.join(OUT, out), Buffer.from(r.data, 'base64'));
}
fs.writeFileSync(path.join(ROOT, 'media-src/manifest.json'), JSON.stringify(manifest, null, 2));
console.table(Object.entries(manifest).flatMap(([k, v]) => v.map((x) => ({ image: x.file, w: x.w, h: x.h, kb: x.kb }))));
await browser.close();
