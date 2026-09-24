// Genera las imágenes Open Graph (1200x630, JPG) de cada idioma con tipografías y fotos reales.
import { chromium } from 'playwright-core';
import fs from 'node:fs';
import path from 'node:path';

const ROOT = path.resolve(import.meta.dirname, '..');
const b64 = (p) => fs.readFileSync(path.join(ROOT, p)).toString('base64');
const fonts = `@font-face{font-family:Cabin;src:url(data:font/woff2;base64,${b64('src/assets/fonts/cabin-latin-var.woff2')}) format("woff2");font-weight:400 700}
@font-face{font-family:Karla;src:url(data:font/woff2;base64,${b64('src/assets/fonts/karla-latin-var.woff2')}) format("woff2");font-weight:400 700}`;
const photo = `data:image/webp;base64,${b64('src/assets/img/jm-castillo-ingeniero-mezcla-mastering-760.webp')}`;
const logo = `data:image/webp;base64,${b64('src/assets/img/logo-jm-castillo.webp')}`;

const variants = {
  es: { out: 'og-mezcla-y-mastering-jm-castillo.jpg', kicker: 'Ingeniero de mezcla y mastering', h: 'Mezcla y mastering<br>que suena a <em>platino</em>', stats: [['37x', 'Platino'], ['38x', 'Oro'], ['3', 'Premios Odeon']], artists: 'RVFV · Rels B · David Bisbal · Camilo · Duki', url: 'jmcastillo.es/mezcla-y-mastering', price: 'Desde 100€ · 3-5 días' },
  en: { out: 'og-mixing-and-mastering-jm-castillo.jpg', kicker: 'Mixing & mastering engineer', h: 'Mixing and mastering<br>that sounds <em>platinum</em>', stats: [['37x', 'Platinum'], ['38x', 'Gold'], ['3', 'Odeon Awards']], artists: 'RVFV · Rels B · David Bisbal · Camilo · Duki', url: 'jmcastillo.es/en/mixing-and-mastering', price: 'From €100 · 3–5 days' },
};

const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome' });
const page = await browser.newPage({ viewport: { width: 1200, height: 630 } });
for (const v of Object.values(variants)) {
  await page.setContent(`<!doctype html><html><head><style>${fonts}
  *{box-sizing:border-box}body{margin:0;width:1200px;height:630px;overflow:hidden;font-family:Karla;color:#fff;background:#e01e2d;position:relative}
  .blob{position:absolute;inset:0;background:radial-gradient(40% 60% at 70% 45%,rgba(26,0,2,.8),transparent 70%),radial-gradient(35% 50% at 30% 115%,rgba(20,0,2,.85),transparent 70%)}
  .photo{position:absolute;right:0;top:0;width:430px;height:630px;background:#0a0a0a}
  .photo img{width:100%;height:100%;object-fit:cover;filter:grayscale(1) contrast(1.08);opacity:.92}
  .photo:after{content:"";position:absolute;inset:0;background:linear-gradient(90deg,#e01e2d 0,rgba(224,30,45,0) 28%)}
  .c{position:absolute;left:72px;top:60px;width:720px}
  .logo{height:40px;filter:brightness(0) invert(1)}
  .k{margin-top:44px;font:700 20px Cabin;letter-spacing:.18em;text-transform:uppercase;display:flex;align-items:center;gap:14px}.k:before{content:"";width:34px;height:3px;background:#fff}
  h1{margin:18px 0 0;font:700 70px/1.02 Cabin;letter-spacing:-1.5px}h1 em{font-style:normal;color:#0a0a0a}
  .s{position:absolute;left:72px;bottom:118px;display:flex;gap:14px}
  .s div{background:#0a0a0a;border-radius:16px;padding:16px 22px}.s b{display:block;font:700 40px/1 Cabin;color:#ff4545}.s i{font-style:normal;font:700 15px Cabin;letter-spacing:.14em;text-transform:uppercase;color:#ddd}
  .a{position:absolute;left:72px;bottom:62px;font:700 21px Cabin;opacity:.95}
  .u{position:absolute;left:72px;bottom:30px;font:600 17px Karla;opacity:.85}
  .p{position:absolute;right:40px;bottom:36px;background:#fff;color:#0a0a0a;font:700 20px Cabin;padding:12px 18px;border-radius:999px}
  </style></head><body><div class="blob"></div><div class="photo"><img src="${photo}"></div>
  <div class="c"><img class="logo" src="${logo}"><div class="k">${v.kicker}</div><h1>${v.h}</h1></div>
  <div class="s">${v.stats.map(([b, i]) => `<div><b>${b}</b><i>${i}</i></div>`).join('')}</div>
  <div class="a">${v.artists}</div><div class="u">${v.url}</div><div class="p">${v.price}</div></body></html>`);
  await page.evaluate(() => document.fonts.ready);
  await page.screenshot({ path: path.join(ROOT, 'src/assets/img', v.out), type: 'jpeg', quality: 86 });
  console.log('OG →', v.out, (fs.statSync(path.join(ROOT, 'src/assets/img', v.out)).size / 1024).toFixed(0), 'KB');
}
await browser.close();
