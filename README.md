# Landing «Mezcla y Mastering» / «Mix & Mastering» · Jm Castillo

Landing bilingüe (ES + EN) para posicionar en Google, y en buscadores con IA (GEO), por **"mezcla y mastering"**, **"mix & mastering"** y sus variantes, y para llevar el tráfico a [jmcastillo.es](https://jmcastillo.es/).

Es HTML/CSS/JS estático, sin dependencias ni build. Funciona en cualquier hosting y también dentro de WordPress.

```
mezcla-y-mastering/index.html      → https://jmcastillo.es/mezcla-y-mastering/      (ES)
en/mix-and-mastering/index.html    → https://jmcastillo.es/en/mix-and-mastering/    (EN)
assets/css/landing.css             → estilos + tokens de marca (colores, tipografías)
assets/js/landing.js               → infografía animada, menú, formulario → email
assets/img/                        → favicon, imágenes OG (1200×630) y huecos para fotos
sitemap-mix-mastering.xml          → sitemap con hreflang + imágenes
robots-snippet.txt                 → línea para añadir al robots.txt actual
llms.txt                           → resumen para LLMs (GEO)
```

## Por qué en subcarpeta de jmcastillo.es (recomendado)

Las URLs canónicas apuntan a `https://jmcastillo.es/mezcla-y-mastering/` y `https://jmcastillo.es/en/mix-and-mastering/`. Al publicarlo en subcarpeta, la landing hereda la autoridad del dominio y la reparte en los dos sentidos. Un dominio o subdominio nuevo empezaría de cero.

**Despliegue:** sube el contenido de este repo a la raíz del hosting de jmcastillo.es. Se crean las carpetas `mezcla-y-mastering/`, `en/mix-and-mastering/` y `assets/`, y no se toca nada de WordPress. Si ya existe una carpeta `/assets/` en el servidor, cámbiale el nombre y actualiza las rutas.

¿Otro dominio? Reemplaza `https://jmcastillo.es/mezcla-y-mastering/` y `https://jmcastillo.es/en/mix-and-mastering/` en los 2 HTML, el sitemap y `llms.txt` (canonical, hreflang, og:url y JSON-LD).

## ⚠️ Pendiente antes de publicar

El entorno donde se generó no tenía acceso a jmcastillo.es (bloqueo de red), así que **no se pudieron descargar tus imágenes ni tus tipografías**. Los datos de marca salen de lo que Google tiene indexado de tu web.

1. **Fotos.** Copia estas imágenes de tu web con estos nombres exactos. Mientras falten, se muestra un fondo de marca y no aparece ninguna imagen rota:
   - `assets/img/jm-castillo-estudio.jpg`: tu foto en el estudio (vertical 4:5, ~800×1000). También se usa en el schema `Person`.
   - `assets/img/mi-luz-rvfv-rels-b.jpg`: portada de «Mi Luz» (cuadrada, ~800×800).
2. **Tipografías y colores.** Edita los tokens de `:root` al principio de `assets/css/landing.css` (`--font-display`, `--font-body`, `--accent`…) y el `<link>` de Google Fonts de los 2 HTML para que coincidan con tu web.
3. **Verifica los datos.** Revisa la lista de artistas (Rels B, RVFV, David Bisbal, Camilo, Duki, Anitta, Lola Indigo) y las cifras de «Mi Luz» (8x Platino, +250M streams). Si tienes más créditos, discos certificados o **reseñas reales** de artistas, añádelos: es lo que más sube el E-E-A-T. No se han inventado precios, plazos ni testimonios.
4. **Enlaces internos a tu web.** Hoy apuntan a la home (`https://jmcastillo.es/`) porque no se pudo leer tu menú. Cuando los tengas, cámbialos por tus URLs reales de *Trabajos/Portfolio*, *Contacto* o *Discografía*. Busca `https://jmcastillo.es/"` en los HTML.
5. Añade la línea de `robots-snippet.txt` a tu robots.txt, y envía el sitemap en Google Search Console y Bing Webmaster Tools.

## Interlinking que tienes que hacer DESDE tu web (muy importante)

Una landing sin enlaces internos entrantes no posiciona. En jmcastillo.es:

- **Menú principal:** añade «Mezcla y Mastering» → `/mezcla-y-mastering/` (y «Mix & Mastering» → `/en/mix-and-mastering/` en la versión inglesa o en el selector de idioma).
- **Home:** en la sección de servicios, enlaza con anchor text descriptivo: *"servicio de mezcla y mastering online"*.
- **Cada trabajo o crédito** (por ejemplo, la página de «Mi Luz» en tustereo.com si es tuya): añade una línea *"¿Quieres este sonido? Mezcla y mastering profesional"* → landing.
- **Footer global:** enlace a las dos versiones.
- **Bio de Instagram, Linktree, SoundCloud, YouTube:** apunta a la landing según el idioma del público.

Dentro de la landing ya hay: índice de contenidos, enlaces contextuales entre secciones, migas de pan hacia la home, cambio ES⇄EN con `hreflang`, bloque «Sigue explorando», footer con enlaces y todos los CTA hacia email o la web.

## Qué incluye (SEO / EEAT / GEO)

| Área | Implementado |
|---|---|
| **On-page** | Title y meta description con la keyword al inicio, H1 único, jerarquía H2/H3 con variantes semánticas (mezcla y masterización, mastering online, stem mastering, LUFS…), URLs limpias por idioma |
| **Internacional** | `hreflang` es / en / x-default (x-default → EN para el mercado internacional), `og:locale`, textos redactados de forma nativa en cada idioma (no traducción literal) |
| **Datos estructurados** | `@graph` JSON-LD: `WebSite`, `Person` (sameAs a redes), `ProfessionalService`, `Service` + `OfferCatalog`, `WebPage` (speakable, dateModified), `BreadcrumbList`, `FAQPage` (10 preguntas) |
| **E-E-A-T** | Autoría clara, sección «Sobre mí» con trayectoria real, caso de éxito verificable («Mi Luz» 8x Platino), créditos de artistas, datos de contacto, fecha de actualización visible |
| **GEO (IA)** | Bloques de «respuesta directa» al inicio de cada sección, definiciones citables, tablas (mezcla vs mastering, LUFS por plataforma), checklist, FAQ, `llms.txt` |
| **UX / Conversión** | Infografía animada interactiva (antes/después) en el hero, CTA fijo en móvil, formulario de presupuesto que prepara el email (evento `generate_lead` si hay GA4), escucha en SoundCloud |
| **Rendimiento / a11y** | Sin frameworks, JS diferido (~8 KB), imágenes lazy con width/height, fuentes con `display=swap`, `prefers-reduced-motion`, skip-link, ARIA, contraste alto |
| **Social** | Open Graph y Twitter Card con imágenes 1200×630 por idioma |

### Mapa de keywords

- **ES:** mezcla y mastering · mezcla y mastering online · mezcla y masterización · ingeniero de mezcla y mastering · mastering para Spotify · precio mezcla y mastering · cómo preparar pistas para mezcla
- **EN:** mix and mastering · mixing and mastering services · online mixing and mastering · mixing engineer for hire · reggaeton / Latin mixing engineer · mastering for Spotify LUFS · how to prepare stems for mixing

### Siguientes pasos recomendados

- Añadir **reseñas reales** (Google Business Profile, SoundBetter…) y marcarlas con `Review` solo si son verificables.
- Incrustar 2 o 3 **antes/después reales** (SoundCloud privado o audio propio): es la prueba de experiencia más potente.
- Crear artículos de blog satélite (*"cómo mezclar voces de reggaetón"*, *"LUFS para Spotify"*) que enlacen a la landing.
- Si más adelante publicas precios fijos, añade `price` y `priceCurrency` a los `Offer` del JSON-LD.
