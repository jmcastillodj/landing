// Auditoría automática del build (dist/). Sale con código 1 si encuentra errores.
// Uso: node scripts/check.mjs [--no-external] [--lighthouse]
import { chromium } from 'playwright-core';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { execFileSync } from 'node:child_process';

const ROOT = path.resolve(import.meta.dirname, '..');
const DIST = path.join(ROOT, 'dist');
const REPORT = path.join(ROOT, 'reports');
const CHROME = process.env.CHROME_PATH || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome';
const ORIGIN = 'https://jmcastillo.es';
const PORT = 4173;
const PAGES = ['/mezcla-y-mastering/', '/en/mixing-and-mastering/'];
fs.mkdirSync(REPORT, { recursive: true });

const errors = [], warnings = [], info = [];
const err = (m) => errors.push(m), warn = (m) => warnings.push(m);

// ---------- Servidor estático con gzip (simula producción) ----------
const types = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.webp': 'image/webp', '.png': 'image/png', '.jpg': 'image/jpeg', '.woff2': 'font/woff2', '.xml': 'application/xml', '.txt': 'text/plain; charset=utf-8', '.mp3': 'audio/mpeg' };
const server = http.createServer((req, res) => {
  let p = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (p.endsWith('/')) p += 'index.html';
  const f = path.join(DIST, p);
  if (!f.startsWith(DIST) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); return res.end('404'); }
  const ext = path.extname(f);
  let body = fs.readFileSync(f);
  const h = { 'content-type': types[ext] || 'application/octet-stream', 'cache-control': ext === '.html' ? 'no-cache' : 'public, max-age=31536000' };
  if (/\.(html|js|css|xml|txt)$/.test(f) && /br/.test(req.headers['accept-encoding'] || '')) { body = zlib.brotliCompressSync(body); h['content-encoding'] = 'br'; }
  res.writeHead(200, h); res.end(body);
}).listen(PORT);
const base = `http://localhost:${PORT}`;

// ---------- Análisis estático ----------
const pagesMeta = {};
for (const p of PAGES) {
  const html = fs.readFileSync(path.join(DIST, p, 'index.html'), 'utf8');
  const get = (re) => (html.match(re) || [])[1];
  const title = get(/<title>([^<]*)<\/title>/);
  const desc = get(/<meta name="description" content="([^"]*)"/);
  const canonical = get(/<link rel="canonical" href="([^"]*)"/);
  const hreflang = Object.fromEntries([...html.matchAll(/<link rel="alternate" hreflang="([^"]+)" href="([^"]+)"/g)].map((m) => [m[1], m[2]]));
  pagesMeta[p] = { canonical, hreflang, html };
  const decoded = (s) => (s || '').replace(/&amp;/g, '&');
  if (!title || decoded(title).length < 30 || decoded(title).length > 65) err(`${p}: title de ${decoded(title).length} caracteres (objetivo 30-65): "${decoded(title)}"`);
  if (!desc || decoded(desc).length < 70 || decoded(desc).length > 165) err(`${p}: meta description de ${decoded(desc).length} caracteres (objetivo 70-165)`);
  if (canonical !== ORIGIN + p) err(`${p}: canonical "${canonical}" no es autorreferente`);
  for (const l of ['es', 'en', 'x-default']) if (!hreflang[l]) err(`${p}: falta hreflang="${l}"`);
  if (!Object.values(hreflang).includes(ORIGIN + p)) err(`${p}: hreflang no incluye la propia URL`);
  for (const t of ['og:title', 'og:description', 'og:image', 'og:url', 'og:type', 'og:locale', 'og:image:alt']) if (!html.includes(`property="${t}"`)) err(`${p}: falta ${t}`);
  for (const t of ['twitter:card', 'twitter:title', 'twitter:image']) if (!html.includes(`name="${t}"`)) err(`${p}: falta ${t}`);
  const og = get(/property="og:image" content="([^"]+)"/);
  if (og && !fs.existsSync(path.join(DIST, og.replace(ORIGIN, '')))) err(`${p}: og:image no existe en dist (${og})`);
  if (get(/property="og:url" content="([^"]+)"/) !== canonical) err(`${p}: og:url ≠ canonical`);
  if ((html.match(/<h1[\s>]/g) || []).length !== 1) err(`${p}: debe haber exactamente un H1`);
  if (!/<meta name="robots" content="index, follow/.test(html)) err(`${p}: meta robots no indexable`);

  // JSON-LD
  const ld = JSON.parse(get(/<script type="application\/ld\+json">([\s\S]*?)<\/script>/));
  const nodes = new Map(ld['@graph'].map((n) => [n['@id'], n]));
  const refs = [];
  const walk = (o) => { if (Array.isArray(o)) return o.forEach(walk); if (o && typeof o === 'object') { if (o['@id'] && Object.keys(o).length === 1) refs.push(o['@id']); Object.values(o).forEach(walk); } };
  walk(ld['@graph']);
  for (const r of refs) if (!nodes.has(r)) err(`${p}: JSON-LD referencia @id inexistente ${r}`);
  const types = ld['@graph'].map((n) => n['@type']);
  for (const t of ['WebSite', 'Person', 'WebPage', 'Service', 'BreadcrumbList', 'FAQPage', 'ImageObject', 'VideoObject', 'ItemList', 'Brand']) if (!types.includes(t)) err(`${p}: JSON-LD sin ${t}`);
  const faq = ld['@graph'].find((n) => n['@type'] === 'FAQPage');
  const visibleQ = (html.match(/<details[\s>]/g) || []).length;
  if (faq.mainEntity.length !== visibleQ) err(`${p}: FAQPage tiene ${faq.mainEntity.length} preguntas y la página muestra ${visibleQ}`);
  const bc = ld['@graph'].find((n) => n['@type'] === 'BreadcrumbList');
  if (bc.itemListElement.at(-1).item !== canonical) err(`${p}: la última miga no es la canonical`);
  const wp = ld['@graph'].find((n) => n['@type'] === 'WebPage');
  if (wp.url !== canonical) err(`${p}: WebPage.url ≠ canonical`);
  if (decoded(title) !== wp.name) warn(`${p}: WebPage.name distinto del <title>`);
  for (const v of ld['@graph'].filter((n) => n['@type'] === 'VideoObject')) for (const k of ['name', 'description', 'thumbnailUrl', 'uploadDate']) if (!v[k]) err(`${p}: VideoObject sin ${k}`);
  for (const o of ld['@graph'].find((n) => n['@type'] === 'Service').hasOfferCatalog.itemListElement) if (!o.price || !o.priceCurrency) err(`${p}: Offer sin precio`);
  // precios del schema = precios visibles
  for (const o of ld['@graph'].find((n) => n['@type'] === 'Service').hasOfferCatalog.itemListElement) {
    const vis = p.startsWith('/en') ? `€${o.price}` : `${o.price}€`;
    if (!html.includes(vis)) err(`${p}: el precio ${vis} del schema no aparece en la página`);
  }
  // Referencias locales (#ancla, /ruta, srcset)
  const ids = new Set([...html.matchAll(/\sid="([^"]+)"/g)].map((m) => m[1]));
  for (const m of html.matchAll(/href="#([^"]*)"/g)) if (m[1] && !ids.has(m[1])) err(`${p}: ancla rota #${m[1]}`);
  const locals = new Set();
  for (const m of html.matchAll(/(?:href|src)="(\/[^"#?]*)"/g)) locals.add(m[1]);
  for (const m of html.matchAll(/srcset="([^"]+)"/g)) m[1].split(',').forEach((s) => locals.add(s.trim().split(' ')[0]));
  for (const l of locals) { let f = path.join(DIST, l); if (l.endsWith('/')) f = path.join(f, 'index.html'); if (!fs.existsSync(f)) err(`${p}: recurso local inexistente ${l}`); }
  // Imágenes
  for (const m of html.matchAll(/<img\b[^>]*>/g)) {
    const tag = m[0];
    if (!/\salt="/.test(tag)) err(`${p}: <img> sin alt: ${tag.slice(0, 80)}`);
    if (!/\swidth="\d+"/.test(tag) || !/\sheight="\d+"/.test(tag)) err(`${p}: <img> sin width/height: ${tag.slice(0, 80)}`);
    if (/\ssrcset="/.test(tag) && !/\ssizes="/.test(tag)) err(`${p}: srcset sin sizes`);
  }
  // Enlaces externos para verificar después
  pagesMeta[p].external = [...new Set([...html.matchAll(/href="(https:\/\/[^"]+)"/g)].map((m) => m[1].replace(/&amp;/g, '&')))];
}
// hreflang recíproco
for (const p of PAGES) for (const [l, u] of Object.entries(pagesMeta[p].hreflang)) {
  const target = PAGES.find((q) => ORIGIN + q === u);
  if (!target) { err(`${p}: hreflang ${l} apunta a URL fuera del proyecto ${u}`); continue; }
  if (!Object.values(pagesMeta[target].hreflang).includes(ORIGIN + p)) err(`hreflang no recíproco: ${target} no enlaza a ${p}`);
}
// Sitemap
const sm = fs.readFileSync(path.join(DIST, 'sitemap-mezcla-y-mastering.xml'), 'utf8');
const locs = [...sm.matchAll(/<loc>([^<]+)<\/loc>/g)].map((m) => m[1]);
for (const p of PAGES) if (!locs.includes(ORIGIN + p)) err(`sitemap: falta ${ORIGIN + p}`);
for (const m of sm.matchAll(/<image:loc>https:\/\/jmcastillo\.es([^<]+)<\/image:loc>/g)) if (!fs.existsSync(path.join(DIST, m[1]))) err(`sitemap: imagen inexistente ${m[1]}`);

// ---------- Navegador: consola, recursos, responsive, accesibilidad ----------
const axeSrc = fs.readFileSync(path.join(ROOT, 'node_modules/axe-core/axe.min.js'), 'utf8');
const browser = await chromium.launch({ executablePath: CHROME });
for (const p of PAGES) {
  for (const [w, h, tag] of [[360, 780, 'mobile'], [820, 1180, 'tablet'], [1440, 900, 'desktop']]) {
    const ctx = await browser.newContext({ viewport: { width: w, height: h }, deviceScaleFactor: tag === 'mobile' ? 2 : 1 });
    const page = await ctx.newPage();
    const failed = [];
    page.on('console', (m) => { if (m.type() === 'error') err(`${p} [${tag}] consola: ${m.text()}`); });
    page.on('pageerror', (e) => err(`${p} [${tag}] error JS: ${e.message}`));
    page.on('requestfailed', (r) => { if (r.url().startsWith(base)) failed.push(r.url()); });
    page.on('response', (r) => { if (r.url().startsWith(base) && r.status() >= 400) failed.push(`${r.status()} ${r.url()}`); });
    await page.goto(base + p, { waitUntil: 'networkidle' });
    // desplazar para cargar lazy images
    await page.evaluate(async () => { for (let y = 0; y < document.body.scrollHeight; y += 500) { scrollTo(0, y); await new Promise((r) => setTimeout(r, 40)); } scrollTo(0, 0); });
    await page.waitForTimeout(600);
    failed.forEach((f) => err(`${p} [${tag}] recurso fallido: ${f}`));
    const res = await page.evaluate(() => ({
      overflow: document.documentElement.scrollWidth - innerWidth,
      broken: [...document.images].filter((i) => i.complete && i.naturalWidth === 0).map((i) => i.currentSrc || i.src),
      headings: [...document.querySelectorAll('h1,h2,h3,h4,h5,h6')].filter((e) => !e.closest('[hidden]')).map((e) => +e.tagName[1]),
      tapSmall: [...document.querySelectorAll('a,button')].filter((e) => { const r = e.getBoundingClientRect(); const s = getComputedStyle(e); return r.width > 0 && s.display !== 'inline' && (r.height < 24 || r.width < 24); }).length,
    }));
    if (res.overflow > 0) err(`${p} [${tag}] scroll horizontal de ${res.overflow}px`);
    res.broken.forEach((b) => err(`${p} [${tag}] imagen rota: ${b}`));
    for (let i = 1; i < res.headings.length; i++) if (res.headings[i] > res.headings[i - 1] + 1) { err(`${p}: salto de encabezado h${res.headings[i - 1]} → h${res.headings[i]}`); break; }
    if (res.tapSmall) warn(`${p} [${tag}] ${res.tapSmall} objetivos táctiles < 24px`);
    if (tag !== 'tablet') {
      await page.addScriptTag({ content: axeSrc });
      const axe = await page.evaluate(async () => (await window.axe.run(document, { runOnly: ['wcag2a', 'wcag2aa', 'wcag21a', 'wcag21aa', 'wcag22aa', 'best-practice'] })).violations.map((v) => ({ id: v.id, impact: v.impact, n: v.nodes.length, sample: v.nodes[0].target.join(' '), help: v.help })));
      for (const v of axe) (['serious', 'critical'].includes(v.impact) ? err : warn)(`${p} [${tag}] axe ${v.impact} ${v.id} (${v.n}): ${v.help} → ${v.sample}`);
    }
    await page.screenshot({ path: path.join(REPORT, `${p.replace(/\//g, '_').replace(/^_|_$/g, '')}-${tag}.png`), fullPage: tag !== 'desktop' ? false : false });
    await ctx.close();
  }
  // Movimiento reducido: la página debe renderizar sin errores y con el instrumento estático
  const ctx = await browser.newContext({ reducedMotion: 'reduce', viewport: { width: 1280, height: 900 } });
  const page = await ctx.newPage();
  page.on('pageerror', (e) => err(`${p} [reduced-motion] error JS: ${e.message}`));
  await page.goto(base + p); await page.waitForTimeout(500);
  await page.click('.ab button[data-state="mix"]');
  await ctx.close();
}
await browser.close();

// ---------- Enlaces externos ----------
if (!process.argv.includes('--no-external')) {
  const all = [...new Set(PAGES.flatMap((p) => pagesMeta[p].external))].filter((u) => !u.startsWith('https://wa.me') && !u.includes('x.com') && !u.includes('instagram.com') && !u.includes('facebook.com'));
  const ours = new Set(PAGES.map((p) => ORIGIN + p)); // aún no publicadas
  for (const u of all) {
    if (ours.has(u)) continue;
    let code = '000';
    try { code = execFileSync('curl', ['-s', '-o', '/dev/null', '-L', '-m', '20', '-A', 'Mozilla/5.0 (compatible; LandingCheck)', '-w', '%{http_code}', u]).toString(); } catch {}
    if (code === '429') warn(`enlace externo ${u} → HTTP 429 (límite de peticiones del servidor, no es un enlace roto)`); else if (code !== '200') err(`enlace externo ${u} → HTTP ${code}`); else info.push(`OK ${u}`);
  }
}

// ---------- Lighthouse ----------
if (process.argv.includes('--lighthouse')) {
  const lh = path.join(ROOT, 'node_modules/.bin/lighthouse');
  const rows = [];
  for (const p of PAGES) for (const preset of ['mobile', 'desktop']) {
    const out = path.join(REPORT, `lighthouse-${p.replace(/\//g, '_').replace(/^_|_$/g, '')}-${preset}.json`);
    const args = [base + p, '--quiet', '--output=json', `--output-path=${out}`, '--chrome-flags=--headless=new --no-sandbox', '--only-categories=performance,accessibility,best-practices,seo'];
    if (preset === 'desktop') args.push('--preset=desktop');
    try { execFileSync(lh, args, { env: { ...process.env, CHROME_PATH: CHROME }, stdio: 'ignore', timeout: 180000 }); } catch (e) { warn(`Lighthouse falló en ${p} ${preset}`); continue; }
    const r = JSON.parse(fs.readFileSync(out, 'utf8'));
    const c = r.categories, a = r.audits;
    rows.push({ page: p, preset, perf: Math.round(c.performance.score * 100), a11y: Math.round(c.accessibility.score * 100), bp: Math.round(c['best-practices'].score * 100), seo: Math.round(c.seo.score * 100), LCP: a['largest-contentful-paint'].displayValue, CLS: a['cumulative-layout-shift'].displayValue, TBT: a['total-blocking-time'].displayValue, FCP: a['first-contentful-paint'].displayValue });
    for (const [id, au] of Object.entries(a)) if (au.score !== null && au.score < 0.9 && au.scoreDisplayMode === 'binary') warn(`LH ${p} ${preset}: ${id} — ${au.title}`);
  }
  console.table(rows);
  fs.writeFileSync(path.join(REPORT, 'lighthouse-summary.json'), JSON.stringify(rows, null, 2));
}

server.close();
console.log(`\n${info.length} comprobaciones externas OK`);
warnings.forEach((w) => console.log('⚠', w));
errors.forEach((e) => console.log('✖', e));
console.log(`\nResultado: ${errors.length} errores, ${warnings.length} avisos`);
process.exit(errors.length ? 1 : 0);
