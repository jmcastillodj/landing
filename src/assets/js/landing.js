// @ts-check
/* Jm Castillo · Mezcla & Mastering — mejora progresiva. La página funciona sin JS. */
import { initScope } from "./scope.js";

const doc = document.documentElement;
doc.classList.add("js");
const reduceMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;
const lang = doc.lang === "es" ? "es" : "en";

/* ---------- Menú móvil ---------- */
const menuBtn = /** @type {HTMLButtonElement|null} */ (document.querySelector(".menu-btn"));
const nav = document.getElementById("nav");
if (menuBtn && nav) {
  const setOpen = (/** @type {boolean} */ open) => {
    nav.classList.toggle("open", open);
    menuBtn.setAttribute("aria-expanded", String(open));
  };
  menuBtn.addEventListener("click", () => setOpen(!nav.classList.contains("open")));
  nav.addEventListener("click", (e) => { if (/** @type {HTMLElement} */ (e.target).closest("a")) setOpen(false); });
  document.addEventListener("keydown", (e) => { if (e.key === "Escape" && nav.classList.contains("open")) { setOpen(false); menuBtn.focus(); } });
}

/* ---------- Reveal: solo elementos que empiezan fuera de pantalla (nada queda oculto en reposo) ---------- */
if (!reduceMotion && "IntersectionObserver" in window) {
  const io = new IntersectionObserver((entries) => {
    for (const en of entries) if (en.isIntersecting) { en.target.classList.add("in"); io.unobserve(en.target); }
  }, { rootMargin: "0px 0px -6% 0px" });
  const vh = innerHeight;
  // primero todas las lecturas de layout, después las escrituras (evita reflows forzados)
  const below = [...document.querySelectorAll("[data-reveal]")].filter((el) => el.getBoundingClientRect().top > vh);
  below.forEach((el) => { el.classList.add("pre"); io.observe(el); });
}

/* ---------- Contadores (el HTML ya trae el valor final) ---------- */
if (!reduceMotion && "IntersectionObserver" in window) {
  const sep = lang === "es" ? "." : ",";
  const fmt = (/** @type {number} */ v) => String(Math.round(v)).replace(/\B(?=(\d{3})+(?!\d))/g, sep);
  const cio = new IntersectionObserver((entries) => {
    for (const en of entries) {
      if (!en.isIntersecting) continue;
      cio.unobserve(en.target);
      const el = /** @type {HTMLElement} */ (en.target);
      const to = Number(el.dataset.count);
      const t0 = performance.now();
      const step = (/** @type {number} */ now) => {
        const k = Math.min((now - t0) / 1400, 1);
        el.textContent = fmt(to * (1 - Math.pow(1 - k, 3)));
        if (k < 1) requestAnimationFrame(step);
      };
      requestAnimationFrame(step);
    }
  }, { threshold: 0.6 });
  document.querySelectorAll("[data-count]").forEach((el) => {
    if (el.getBoundingClientRect().top > innerHeight) cio.observe(el);
  });
}

/* ---------- CTA fijo en móvil: visible fuera del hero y del CTA final ---------- */
const mcta = document.querySelector(".mobile-cta");
const hero = document.querySelector(".hero");
const finalCta = document.querySelector("[data-final-cta]");
if (mcta && hero && "IntersectionObserver" in window) {
  let heroIn = true, ctaIn = false;
  const upd = () => mcta.classList.toggle("show", !heroIn && !ctaIn);
  new IntersectionObserver(([e]) => { heroIn = e.isIntersecting; upd(); }).observe(hero);
  if (finalCta) new IntersectionObserver(([e]) => { ctaIn = e.isIntersecting; upd(); }).observe(finalCta);
}

/* ---------- Índice lateral: sección activa ---------- */
const kbLinks = /** @type {NodeListOf<HTMLAnchorElement>} */ (document.querySelectorAll(".kb-nav a"));
if (kbLinks.length && "IntersectionObserver" in window) {
  const map = new Map();
  kbLinks.forEach((a) => { const t = document.getElementById(a.hash.slice(1)); if (t) map.set(t, a); });
  const kio = new IntersectionObserver((entries) => {
    for (const en of entries) if (en.isIntersecting) {
      kbLinks.forEach((a) => a.removeAttribute("aria-current"));
      map.get(en.target)?.setAttribute("aria-current", "true");
    }
  }, { rootMargin: "-20% 0px -70% 0px" });
  map.forEach((_, t) => kio.observe(t));
}

/* ---------- Copiar email ---------- */
document.querySelectorAll("[data-copy]").forEach((btn) => {
  btn.addEventListener("click", async () => {
    const b = /** @type {HTMLButtonElement} */ (btn);
    const text = b.dataset.copy || "";
    const done = b.dataset.done || "OK";
    const label = b.textContent;
    try { await navigator.clipboard.writeText(text); b.textContent = done; }
    catch { const r = document.createRange(); const code = b.previousElementSibling; if (code) { r.selectNodeContents(code); getSelection()?.removeAllRanges(); getSelection()?.addRange(r); } }
    setTimeout(() => { b.textContent = label; }, 2000);
  });
});

/* ---------- Vídeos: fachada ligera de YouTube (el iframe solo se carga al hacer clic) ---------- */
document.querySelectorAll("a[data-yt]").forEach((a) => {
  a.addEventListener("click", (e) => {
    const link = /** @type {HTMLAnchorElement} */ (a);
    const id = link.dataset.yt;
    if (!id || link.dataset.external === "true") return;
    e.preventDefault();
    const iframe = document.createElement("iframe");
    iframe.src = `https://www.youtube-nocookie.com/embed/${id}?autoplay=1&rel=0`;
    iframe.title = link.getAttribute("aria-label") || "YouTube";
    iframe.allow = "accelerometer; autoplay; encrypted-media; gyroscope; picture-in-picture; fullscreen";
    iframe.allowFullscreen = true;
    link.replaceChildren(iframe);
    link.removeAttribute("href");
  });
});

/* ---------- Eventos de conversión (solo si el sitio tiene GA4/gtag con consentimiento) ---------- */
document.addEventListener("click", (e) => {
  const a = /** @type {HTMLElement} */ (e.target).closest("[data-cta]");
  const w = /** @type {any} */ (window);
  if (a && typeof w.gtag === "function") w.gtag("event", "generate_lead", { method: /** @type {HTMLElement} */ (a).dataset.cta });
});

/* ---------- Instrumento del hero ---------- */
const scope = document.querySelector("[data-scope]");
if (scope) {
  // arranca cuando el navegador está libre para no competir con el LCP ni con la primera interacción
  const boot = () => initScope(/** @type {HTMLElement} */ (scope), reduceMotion);
  if ("requestIdleCallback" in window) requestIdleCallback(boot, { timeout: 1200 }); else setTimeout(boot, 300);
}

/* ---------- Antes / después: se carga solo si existe la sección ---------- */
const abPlayers = document.querySelectorAll("[data-ab]");
if (abPlayers.length) {
  import("./ab-player.js").then(({ initAB }) => abPlayers.forEach((p) => initAB(/** @type {HTMLElement} */ (p))));
}

/* ---------- Año ---------- */
const y = document.querySelector("[data-year]");
if (y) y.textContent = String(new Date().getFullYear());
