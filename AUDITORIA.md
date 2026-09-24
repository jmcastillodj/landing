# Auditoría final · Landing «Mezcla y mastering» / «Mixing and mastering»

Fecha: 24/09/2026 · Build auditado: `dist/` · Herramientas: Lighthouse 12, axe-core 4 (WCAG 2.2 AA), html-validate 8, TypeScript 5.6 (`checkJs` estricto), `scripts/check.mjs` (Chromium).

## 1. Resultados medidos

| Página | Rendimiento | Accesibilidad | Buenas prácticas | SEO | LCP | CLS | TBT |
|---|---|---|---|---|---|---|---|
| ES · móvil (4G simulado) | **100** | **100** | **100** | **100** | 1,4 s | 0 | 60 ms |
| ES · escritorio | **100** | **100** | **100** | **100** | 0,4 s | 0 | 0 ms |
| EN · móvil (4G simulado) | **100** | **100** | **100** | **100** | 1,4 s | 0 | 0 ms |
| EN · escritorio | **100** | **100** | **100** | **100** | 0,4 s | 0 | 0 ms |

| Comprobación | Resultado |
|---|---|
| TypeScript (`tsc --checkJs --strict`) | 0 errores |
| html-validate (recommended + WCAG) | 0 errores |
| axe-core WCAG 2.2 AA, móvil y escritorio | 0 violaciones |
| Errores de consola / JS (360, 820 y 1440 px, y con reduced-motion) | 0 |
| Scroll horizontal (360 / 820 / 1440 px) | 0 px |
| Imágenes rotas / recursos 404 | 0 |
| Anclas internas rotas | 0 |
| Enlaces externos (28, incluidas 16 fichas de /artistas/) | 28 OK · 2 vídeos de YouTube devuelven 429 al bot (límite de peticiones; verificados por oEmbed) |
| Canonical autorreferente y absoluto | OK en ambas |
| hreflang es/en/x-default recíproco | OK |
| Sitemap = canonicals + alternates + imágenes existentes | OK |
| JSON-LD: parseo, referencias `@id`, paridad FAQ visible/schema, precios del schema visibles en la página, migas = canonical | OK |
| Peso | HTML 23 KB gzip (CSS crítico inline) · JS 3,4 KB gzip · 0 peticiones a terceros en la carga |

## 2. Problemas encontrados en la auditoría y corregidos

1. **Scroll horizontal de 218 px en móvil**: la tabla (min-width 560 px) ensanchaba la columna del grid (`1fr` → `minmax(0,1fr)`).
2. **Contraste WCAG del rojo**: blanco sobre `#FF2D2D` = 3,7:1 (no pasa AA). Los fondos con texto usan el segundo rojo corporativo `#E01E2D` (4,8:1); el texto rojo sobre blanco usa `#D11A28` (5,4:1) y sobre negro `#FF4545` (5,6:1).
3. **Enlaces distinguibles solo por color** en el pie legal → subrayados.
4. **CTA fijo móvil fuera de landmarks** → `<aside aria-label>`.
5. **Tablas con scroll no accesibles por teclado** → `<section tabindex="0" aria-label>`.
6. **Región `aria-live` mal colocada tras `<figcaption>`**, `aria-label` en `<ul>` y `DOCTYPE` → corregidos (html-validate).
7. **Etiquetas del goniómetro a tamaño erróneo** (CSS en px sobre unidades SVG) → corregido.
8. **Precios en gris** por especificidad CSS → blanco.
9. **INP / hilo principal**: reflows forzados (lecturas/escrituras de layout intercaladas) y animación compitiendo con la carga → lecturas agrupadas, arranque en `requestIdleCallback`, 30 fps. Max-potential-FID: 210 ms → 60–110 ms.
10. **CLS por swap de fuentes**: métricas de fallback medidas en Chromium (`size-adjust` 90,7 % / 101,6 %, `ascent/descent-override`) → CLS 0.
11. **Icono play/pausa** del reproductor A/B no cambiaba → corregido.
12. **Dato incorrecto** («Mi Luz» 6x → 8x) corregido en todo el proyecto.

## 3. Puntuación por área (estimación razonada)

| Área | Nota | Justificación |
|---|---|---|
| SEO on-page | 9,5/10 | Title/description orientados a intención y CTR, H1 único, jerarquía H2→H4 sin saltos, cobertura semántica (mezcla musical, mezcla de voces, mastering, stem mastering, LUFS, stems, ingeniero de mezcla…), respuestas directas por sección. Resta 0,5 hasta resolver la canibalización con la home (ver §5). |
| Performance | 10/10 | 100 en Lighthouse; LCP de texto (sin imagen en el hero), fuentes autoalojadas y precargadas, sin terceros, WebP responsive, JS diferido de 3,4 KB. |
| Accesibilidad | 10/10 | axe 0 violaciones, teclado, foco visible, `prefers-reduced-motion`, ARIA en controles, alt descriptivos. |
| Best practices | 10/10 | HTTPS, CSP y cabeceras de seguridad en `.htaccess`, sin errores de consola. |
| SEO técnico | 9,5/10 | Canonical, hreflang, sitemap con imágenes, robots, caché/compresión, redirecciones de variantes. Pendiente de despliegue real y alta en Search Console. |
| Schema | 9,5/10 | Grafo conectado por `@id`: WebSite, Person (+Occupation, ContactPoint, premios), Brand, Service + OfferCatalog (7 precios reales), ItemList de MusicRecording certificados con Role «Mezcla y mastering», VideoObject ×2, ImageObject, WebPage, BreadcrumbList, FAQPage. |
| Internacionalización | 9,5/10 | URLs propias por idioma, textos localizados (no traducidos), keywords de cada mercado, hreflang bidireccional + x-default, CTA adaptados. Limitación: la reserva y las fichas enlazadas están en español (se avisa al usuario). |
| GEO / AI Search | 9,5/10 | Definiciones en la primera frase de cada bloque, tablas comparativas, datos verificables con fuente enlazada, entidades claras, `llms.txt`. |
| CRO | 9/10 | Jerarquía Hero → autoridad → trabajos → guía → proceso → tarifas → experiencia → FAQ → CTA; 7 puntos de conversión; precio visible; WhatsApp con mensaje precargado; CTA fijo en móvil. Subiría a 10 con testimonios reales y audios antes/después. |
| UX/UI | 9,5/10 | Identidad de jmcastillo.es, instrumento del hero propio del oficio, lista de precios en vez de rejilla de tarjetas, sección de conocimiento con índice lateral. |

## 4. Decisiones de schema (y lo que NO se ha marcado)

- **Sin LocalBusiness/ProfessionalService**: la web no publica una dirección visitable y el servicio es online; marcarlo como negocio local sería inexacto. La entidad principal es **Person** (Jm Castillo), proveedor del **Service**, con **Brand** para el logo.
- **Sin `aggregateRating` ni `Review`**: no hay reseñas publicadas verificables.
- **FAQPage** se incluye como marcado semántico; Google limita los rich results de FAQ a webs de autoridad/gobierno, así que no se asume que aparezcan.
- **VideoObject** con fechas de estreno reales de YouTube (26/06/2024 y 09/05/2025).

## 5. Pendiente (requiere acción tuya)

1. **Canibalización con la home**: su title es «Estudio de Mezcla y Mastering en España». Recomendado: orientar la home a marca («Jm Castillo · Ingeniero de mezcla y mastering con 37 discos de Platino») y enlazar desde ella a `/mezcla-y-mastering/`.
2. **Dato inconsistente en la web**: la home dice 36x Platino; /certificaciones/ y /servicios/ dicen 37x. La landing usa 37x.
3. **Post de «Mi Luz»**: título y URL dicen «6x»; ahora son 8x.
4. **Audios antes/después**: la sección está programada y probada pero oculta hasta tener audios reales (ver `src/data/README.md`).
5. **Testimonios**: no se ha encontrado ninguno publicado; si tienes mensajes reales de artistas con permiso, añadirlos subiría la conversión.
6. **Analítica**: la landing no incluye GA4 (no hay ID ni consentimiento configurado). Si se añade, debe respetar el banner de cookies (Complianz) y ampliar la CSP.
