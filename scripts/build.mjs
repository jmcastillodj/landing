// Build de producción de la landing: CSS crítico inline, JS minificado con hash, FAQ/AB generados desde datos.
// Uso: node scripts/build.mjs            → dist/  (para subir a la raíz de jmcastillo.es)
//      node scripts/build.mjs --preview  → preview/ (autocontenido, para la vista previa)
import * as esbuild from 'esbuild';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';

const ROOT = path.resolve(import.meta.dirname, '..');
const SRC = path.join(ROOT, 'src');
const PREVIEW = process.argv.includes('--preview');
const OUT = path.join(ROOT, PREVIEW ? 'preview' : 'dist');
const PAGES = [
  { lang: 'es', file: 'mezcla-y-mastering/index.html', faq: 'faq.es.json', previewOut: 'index.html' },
  { lang: 'en', file: 'en/mixing-and-mastering/index.html', faq: 'faq.en.json', previewOut: 'en.html' },
];

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });

const copyDir = (from, to, filter = () => true) => {
  if (!fs.existsSync(from)) return;
  fs.mkdirSync(to, { recursive: true });
  for (const e of fs.readdirSync(from, { withFileTypes: true })) {
    const s = path.join(from, e.name), d = path.join(to, e.name);
    if (e.isDirectory()) copyDir(s, d, filter); else if (filter(e.name)) fs.copyFileSync(s, d);
  }
};

// ---------- Assets estáticos ----------
copyDir(path.join(SRC, 'assets/img'), path.join(OUT, 'assets/img'));
if (!PREVIEW) copyDir(path.join(SRC, 'assets/fonts'), path.join(OUT, 'assets/fonts'));
copyDir(path.join(SRC, 'assets/audio'), path.join(OUT, 'assets/audio'), (n) => !n.startsWith('.'));

// ---------- CSS ----------
const cssRes = await esbuild.build({
  entryPoints: [path.join(SRC, 'assets/css/landing.css')],
  bundle: true, minify: true, write: false, external: ['/assets/*'], target: ['chrome100', 'safari15', 'firefox100'],
});
let css = cssRes.outputFiles[0].text;
if (PREVIEW) css = css.replace(/@font-face\{font-family:(Cabin|Karla);src:url\([^)]*\)[^}]*\}/g, '');

// ---------- JS ----------
let scriptTag;
if (PREVIEW) {
  const js = await esbuild.build({ entryPoints: [path.join(SRC, 'assets/js/landing.js')], bundle: true, minify: true, format: 'iife', write: false, target: 'es2020' });
  scriptTag = `<script>${js.outputFiles[0].text.replace(/<\/script/gi, '<\\/script')}</script>`;
} else {
  const js = await esbuild.build({
    entryPoints: [path.join(SRC, 'assets/js/landing.js')], bundle: true, splitting: true, format: 'esm', minify: true,
    outdir: path.join(OUT, 'assets/js'), entryNames: '[name]-[hash]', chunkNames: '[name]-[hash]', metafile: true, target: 'es2020',
  });
  const entry = Object.entries(js.metafile.outputs).find(([, o]) => o.entryPoint?.endsWith('landing.js'))[0];
  scriptTag = `<script type="module" src="/${path.relative(OUT, path.join(ROOT, entry)).split(path.sep).join('/')}"></script>`;
}

// ---------- Helpers de contenido ----------
const decode = (s) => s.replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'");
const text = (html) => decode(html.replace(/<[^>]+>/g, '')).replace(/\s+/g, ' ').trim();
const esc = (s) => s.replace(/&(?!amp;|lt;|gt;|quot;|#)/g, '&amp;');
const abData = JSON.parse(fs.readFileSync(path.join(SRC, 'data/ab-examples.json'), 'utf8'));
const L = {
  es: { play: 'Reproducir', pause: 'Pausar', loading: 'Cargando audio', pos: 'Posición de reproducción', before: 'Antes', after: 'Después', group: 'Versión', lm: 'Comparación a volumen igualado para que el master no gane solo por sonar más fuerte.' },
  en: { play: 'Play', pause: 'Pause', loading: 'Loading audio', pos: 'Playback position', before: 'Before', after: 'After', group: 'Version', lm: 'Level-matched comparison, so the master doesn\'t win just by being louder.' },
};

const report = [];
for (const page of PAGES) {
  let html = fs.readFileSync(path.join(SRC, page.file), 'utf8');
  const t = L[page.lang];

  // FAQ → HTML visible + JSON-LD (una sola fuente)
  const faq = JSON.parse(fs.readFileSync(path.join(SRC, 'data', page.faq), 'utf8'));
  let first = true;
  const faqHtml = '<div class="faq-groups">' + faq.map((g) => `<div class="faq" data-reveal><h3>${esc(g.group)}</h3>` + g.items.map((it) => {
    const open = first ? ' open' : ''; first = false;
    return `<details${open}><summary>${esc(it.q)}</summary><div class="answer">${it.a}</div></details>`;
  }).join('') + '</div>').join('') + '</div>';
  const faqLd = faq.flatMap((g) => g.items).map((it) => ({ '@type': 'Question', name: it.q, acceptedAnswer: { '@type': 'Answer', text: text(it.a) } }));
  html = html.replace('<!--FAQ:HTML-->', faqHtml).replace('"FAQ_JSONLD"', JSON.stringify(faqLd));

  // Antes/después: solo si hay audios reales configurados
  if (!abData.length) {
    html = html.replace(/\s*<!--AB:START-->[\s\S]*?<!--AB:END-->/, '');
  } else {
    const items = abData.map((d, i) => `
        <div class="ab-player" data-ab data-a="${d.a}" data-b="${d.b}" data-gain-a="${d.gainA || 0}" data-gain-b="${d.gainB || 0}" data-pause-label="${t.pause}" data-loading-label="${t.loading}">
          <div class="ab-head"><h3>${esc(d.title[page.lang])}</h3><p>${esc(d.credit[page.lang])}</p></div>
          <div class="ab-wave"><canvas aria-hidden="true"></canvas><input type="range" min="0" max="1000" value="0" step="1" aria-label="${t.pos}" id="ab-seek-${i}"></div>
          <div class="ab-controls">
            <button class="ab-play" type="button" aria-label="${t.play}"><svg aria-hidden="true"><use href="#i-play"/></svg></button>
            <span class="ab-time">0:00 / 0:00</span>
            <div class="ab-switch" role="group" aria-label="${t.group}"><button type="button" data-v="a" aria-pressed="true">${t.before}</button><button type="button" data-v="b" aria-pressed="false">${t.after}</button></div>
          </div>
          ${d.levelMatched ? `<p class="ab-note">${t.lm}</p>` : ''}
        </div>`).join('');
    html = html.replace('<!--AB:ITEMS-->', items).replace(/<!--AB:(START|END)-->/g, '');
  }

  // CSS inline + JS con hash
  html = html.replace('<link rel="stylesheet" href="/assets/css/landing.css">', `<style>${css}</style>`);
  html = html.replace('<script type="module" src="/assets/js/landing.js"></script>', PREVIEW ? '' : scriptTag);
  if (PREVIEW) {
    html = html.replace('</body>', `${scriptTag}\n</body>`);
    html = html.replace(/<link rel="preload" href="\/assets\/fonts[^>]+>\n?/g, '');
    html = html.replace('</title>', '</title>\n<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Cabin:wght@400..700&family=Karla:wght@400..700&display=swap">');
    html = html.replace(/href="\/en\/mixing-and-mastering\/"/g, 'href="en.html"').replace(/href="\/mezcla-y-mastering\/"/g, 'href="index.html"');
    html = html.replace(/(["(\s,])\/assets\//g, '$1assets/');
    html = html.replace(/data-yt="/g, 'data-external="true" data-yt="');
  }

  // Validación JSON-LD
  const ld = html.match(/<script type="application\/ld\+json">([\s\S]*?)<\/script>/)[1];
  JSON.parse(ld);

  const outFile = path.join(OUT, PREVIEW ? page.previewOut : page.file);
  fs.mkdirSync(path.dirname(outFile), { recursive: true });
  fs.writeFileSync(outFile, html);
  report.push({ page: path.relative(OUT, outFile), kb: +(html.length / 1024).toFixed(1), gzipKb: +(zlib.gzipSync(html).length / 1024).toFixed(1), brotliKb: +(zlib.brotliCompressSync(html).length / 1024).toFixed(1) });
}

// Archivos raíz (sitemap, llms.txt, .htaccess por carpeta)
if (!PREVIEW) copyDir(path.join(SRC, 'root'), OUT);

for (const f of PREVIEW ? [] : fs.readdirSync(path.join(OUT, 'assets/js')).filter((f) => f.endsWith('.js'))) {
  const b = fs.readFileSync(path.join(OUT, 'assets/js', f));
  report.push({ page: 'assets/js/' + f, kb: +(b.length / 1024).toFixed(1), gzipKb: +(zlib.gzipSync(b).length / 1024).toFixed(1), brotliKb: +(zlib.brotliCompressSync(b).length / 1024).toFixed(1) });
}
console.table(report);
console.log(`CSS inline: ${(css.length / 1024).toFixed(1)} KB (${(zlib.gzipSync(css).length / 1024).toFixed(1)} KB gzip) · build → ${path.relative(ROOT, OUT)}/`);
