# Landing «Mezcla y mastering» · «Mixing and mastering» — Jm Castillo

Landing bilingüe para posicionar en Google y en buscadores con IA por **mezcla y mastering / mix & mastering / mixing and mastering**, y convertir ese tráfico hacia [jmcastillo.es](https://jmcastillo.es/) (reserva en `/servicios/`, WhatsApp y `/contacto/`).

- ES: `https://jmcastillo.es/mezcla-y-mastering/`
- EN: `https://jmcastillo.es/en/mixing-and-mastering/`

HTML estático sin frameworks: 100/100/100/100 en Lighthouse (móvil y escritorio). El detalle está en [AUDITORIA.md](AUDITORIA.md).

## Estructura

```
src/                         ← fuente (se edita aquí)
  mezcla-y-mastering/index.html
  en/mixing-and-mastering/index.html
  assets/css/landing.css     ← tokens de marca (rojo #FF2D2D/#E01E2D, Cabin + Karla)
  assets/js/landing.js       ← menú, reveal, contadores, vídeos, copiar email
  assets/js/scope.js         ← instrumento del hero (goniómetro + loudness, Canvas)
  assets/js/ab-player.js     ← comparador antes/después (Web Audio, carga bajo demanda)
  assets/img/ · assets/fonts/ · assets/audio/
  data/faq.es.json · faq.en.json · ab-examples.json
  root/                      ← sitemap, llms.txt y .htaccess por carpeta
media-src/                   ← originales descargados de jmcastillo.es (no se publican)
scripts/                     ← build, imágenes, OG y auditoría
deploy/                      ← fragmentos para el .htaccess raíz y robots.txt (Yoast)
dist/                        ← RESULTADO LISTO PARA SUBIR
```

## Comandos

```bash
npm install
npm run build           # genera dist/ (CSS inline, JS con hash, FAQ + JSON-LD desde datos)
npm run typecheck       # TypeScript --checkJs strict
npm run lint            # html-validate sobre dist/
npm run check           # auditoría: consola, 404, responsive, axe WCAG, canonical, hreflang, sitemap, JSON-LD, enlaces
node scripts/check.mjs --lighthouse   # + Lighthouse
node scripts/images.mjs # regenera WebP responsive desde media-src/
node scripts/og.mjs     # regenera las imágenes Open Graph
```

## Despliegue en jmcastillo.es (LiteSpeed + WordPress)

1. `npm run build`.
2. Sube **el contenido de `dist/`** a la raíz del hosting (junto a `wp-content`). Se crean `mezcla-y-mastering/`, `en/mixing-and-mastering/`, `assets/`, `sitemap-mezcla-y-mastering.xml` y `llms.txt`. WordPress no se toca: sus reglas no reescriben carpetas físicas.
3. Pega `deploy/htaccess-raiz.txt` en el `.htaccess` raíz **antes** de `# BEGIN WordPress` (redirecciones 301 de variantes como `/mezcla-mastering/` o `/en/mix-and-mastering/`).
4. Yoast → Herramientas → Editor de archivos: añade la línea de `deploy/robots-yoast.txt`.
5. Search Console: envía `sitemap-mezcla-y-mastering.xml` e inspecciona las dos URLs.
6. Comprueba cabeceras: `curl -I https://jmcastillo.es/mezcla-y-mastering/`. Debe aparecer `content-security-policy`, y los assets con `cache-control: max-age=31536000`.

## Mapa de enlazado interno

**Desde la landing hacia la web** (anchors variados, sin abusar de la exact-match):

| Destino | Anchors usados | Dónde |
|---|---|---|
| `/servicios/` | «Solicita tu mezcla», «Reservar», «Elige tu plan y reserva», «servicios» | nav, hero, trabajos, tarifas (×7), CTA final, CTA móvil |
| `/certificaciones/` | «Certificaciones», «mis certificaciones», «Discos de Platino y Oro» | nav, trabajos, pie |
| `/trabajos/` | «Trabajos», «trabajos», «y muchos más», «Últimos trabajos» | nav, créditos, trabajos, pie |
| `/nosotros/` | «Sobre mí», «Conoce toda mi trayectoria» | nav, experiencia |
| `/contacto/` | «Contacto», «Formulario de contacto» | nav, CTA final, pie |
| `/mixing/` | «guía sobre qué es el mix en una canción», «Qué es el mix» | guía, FAQ, pie |
| `/mastering/` | «guía del mastering», «Qué es el mastering» | guía, FAQ, pie |
| `/artistas/*` (16 fichas) | nombre del artista | créditos, portadas, FAQ |
| post «Mi Luz» | «La historia de «Mi Luz»» | caso destacado |
| `/blog/` | «Blog» | pie |

**Desde la web hacia la landing (tienes que añadirlos en WordPress):**

| Página de jmcastillo.es | Anchor sugerido | Destino |
|---|---|---|
| Menú principal (Servicios → submenú) | Mezcla y mastering | `/mezcla-y-mastering/` |
| Home, bloque «Mixing / Mastering» | mezcla y mastering online | `/mezcla-y-mastering/` |
| `/servicios/` (arriba de las tarifas) | ¿Mezcla o mastering? Te lo explico aquí | `/mezcla-y-mastering/` |
| `/mixing/` y `/mastering/` (al final) | precios y proceso de mezcla y mastering | `/mezcla-y-mastering/#tarifas` |
| Fichas de `/artistas/` | mezcla y mastering de Jm Castillo | `/mezcla-y-mastering/` |
| Posts del blog | ¿Quieres este sonido? | `/mezcla-y-mastering/` |
| Pie global | Mix & mastering in English | `/en/mixing-and-mastering/` |
| Bio de Instagram / YouTube | — | según el idioma del público |

## Mapa de keywords por sección

| Sección | ES | EN |
|---|---|---|
| Title/H1 | mezcla y mastering, mezcla y mastering online, mastering profesional | mixing and mastering, mixing and mastering services, online mixing and mastering |
| Hero/créditos | ingeniero de mezcla y mastering, ingeniero de mezcla reggaetón | mixing engineer, mastering engineer, Latin urban mixing engineer |
| Guía | qué es la mezcla musical, qué es el mastering, diferencia entre mezcla y mastering, mezcla de voces, stem mastering | what is mixing, what is mastering, mixing vs mastering, vocal mixing, stem mastering |
| Loudness | a cuántos LUFS masterizar para Spotify | how loud should a master be for Spotify, LUFS |
| Proceso/envío | cómo enviar pistas para mezcla, cómo exportar stems | how to prepare stems for mixing |
| Tarifas | cuánto cuesta mezclar y masterizar una canción, precio mezcla y mastering | how much does mixing and mastering cost |

## Contenido y veracidad

Todo el contenido procede de jmcastillo.es (servicios, certificaciones, sobre mí, home) o de datos que has confirmado («Mi Luz» 8x Platino). No se han inventado testimonios, reseñas, cifras ni clientes. Los valores del instrumento del hero están rotulados como «ilustrativos».

## Pendientes

Los tienes en [AUDITORIA.md §5](AUDITORIA.md): canibalización con la home, 36x vs 37x, título del post de «Mi Luz», audios antes/después, testimonios reales y analítica con consentimiento.
