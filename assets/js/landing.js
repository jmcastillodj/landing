/* Jm Castillo · Mix & Mastering landing — progressive enhancement only. */
(function () {
  "use strict";
  document.documentElement.classList.remove("no-js");
  var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* ---------- Mobile nav ---------- */
  var btn = document.querySelector(".menu-btn");
  var nav = document.getElementById("nav");
  if (btn && nav) {
    btn.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      btn.setAttribute("aria-expanded", open ? "true" : "false");
    });
    nav.addEventListener("click", function (e) {
      if (e.target.closest("a")) { nav.classList.remove("open"); btn.setAttribute("aria-expanded", "false"); }
    });
  }

  /* ---------- Reveal on scroll ---------- */
  var revealEls = document.querySelectorAll(".reveal");
  if ("IntersectionObserver" in window && !reduce) {
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { if (en.isIntersecting) { en.target.classList.add("in"); io.unobserve(en.target); } });
    }, { rootMargin: "0px 0px -8% 0px" });
    revealEls.forEach(function (el) { io.observe(el); });
  } else {
    revealEls.forEach(function (el) { el.classList.add("in"); });
  }

  /* ---------- Sticky mobile CTA ---------- */
  var mcta = document.querySelector(".mobile-cta");
  var hero = document.querySelector(".hero");
  var contact = document.getElementById("contacto") || document.getElementById("contact");
  if (mcta && hero && "IntersectionObserver" in window) {
    var heroVisible = true, contactVisible = false;
    var upd = function () { mcta.classList.toggle("show", !heroVisible && !contactVisible); };
    new IntersectionObserver(function (e) { heroVisible = e[0].isIntersecting; upd(); }).observe(hero);
    if (contact) new IntersectionObserver(function (e) { contactVisible = e[0].isIntersecting; upd(); }).observe(contact);
  }

  /* ---------- Count-up de cifras ---------- */
  var nums = document.querySelectorAll("[data-count]");
  function runCount(el) {
    var to = parseFloat(el.dataset.count), pre = el.dataset.pre || "", suf = el.dataset.suf || "";
    var sep = document.documentElement.lang === "es" ? "." : ",";
    var fmt = function (v) { return pre + String(Math.round(v)).replace(/\B(?=(\d{3})+(?!\d))/g, sep) + suf; };
    if (reduce) { el.textContent = fmt(to); return; }
    var t0 = performance.now(), dur = 1600;
    (function step(now) {
      var k = Math.min((now - t0) / dur, 1), e = 1 - Math.pow(1 - k, 3);
      el.textContent = fmt(to * e);
      if (k < 1) requestAnimationFrame(step);
    })(t0);
  }
  if ("IntersectionObserver" in window) {
    var cio = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) { if (en.isIntersecting) { runCount(en.target); cio.unobserve(en.target); } });
    }, { threshold: 0.4 });
    nums.forEach(function (n) { cio.observe(n); });
  }

  /* ---------- Analytics: clics de conversión (si hay GA4) ---------- */
  document.addEventListener("click", function (e) {
    var a = e.target.closest("a[data-cta]");
    if (a && window.gtag) window.gtag("event", "generate_lead", { method: a.dataset.cta, link_url: a.href });
  });

  /* ---------- Year ---------- */
  var y = document.querySelector("[data-year]");
  if (y) y.textContent = new Date().getFullYear();

  /* ---------- Hero infographic: mix → master console ---------- */
  var consoleEl = document.querySelector(".console");
  if (!consoleEl) return;

  var W = 560, H = 180, MID = H / 2, N = 140;
  var wavePath = consoleEl.querySelector("#wave");
  var wavePath2 = consoleEl.querySelector("#wave-mirror");
  var bars = Array.prototype.slice.call(consoleEl.querySelectorAll(".spectrum span"));
  var abBtns = consoleEl.querySelectorAll(".ab button");
  var chain = consoleEl.querySelectorAll(".chain li");
  var outLufs = consoleEl.querySelector("[data-out=lufs]");
  var outTp = consoleEl.querySelector("[data-out=tp]");
  var outDr = consoleEl.querySelector("[data-out=dr]");
  var outW = consoleEl.querySelector("[data-out=width]");
  var lufsFill = consoleEl.querySelector(".lufs-bar i");
  var stateLabel = consoleEl.querySelector("[data-state-label]");

  // Target shapes (0..1 amplitude per column)
  function seeded(i, s) { var x = Math.sin(i * 12.9898 + s * 78.233) * 43758.5453; return x - Math.floor(x); }
  var before = [], after = [];
  for (var i = 0; i < N; i++) {
    var beat = (i % 17 < 2) ? 1 : 0;
    var raw = 0.18 + seeded(i, 1) * 0.28 + beat * 0.5 + Math.sin(i / 9) * 0.06;
    before.push(Math.min(raw, 1));
    // mastered: fuller body, peaks controlled under ceiling
    var body = 0.58 + seeded(i, 2) * 0.16 + beat * 0.18 + Math.sin(i / 9) * 0.04;
    after.push(Math.min(body, 0.86));
  }
  // Spectrum profiles (low → high), 24 bands
  var specBefore = [0.55, 0.78, 0.92, 0.88, 0.8, 0.7, 0.62, 0.55, 0.5, 0.52, 0.58, 0.66, 0.74, 0.62, 0.48, 0.4, 0.34, 0.3, 0.26, 0.22, 0.18, 0.14, 0.1, 0.07];
  var specAfter  = [0.72, 0.84, 0.82, 0.74, 0.66, 0.62, 0.6, 0.58, 0.57, 0.56, 0.56, 0.55, 0.55, 0.54, 0.52, 0.5, 0.48, 0.45, 0.42, 0.38, 0.34, 0.3, 0.26, 0.2];

  var readings = {
    mix:    { lufs: -19.6, tp: -3.8, dr: 13, width: 68, chain: 2 },
    master: { lufs: -9.8,  tp: -1.0, dr: 8,  width: 92, chain: 4 }
  };

  var mix = 0;           // 0 = mix (antes), 1 = master (después)
  var target = 0;
  var offset = 0;
  var auto = !reduce;
  var lastSwitch = performance.now();

  function setState(isMaster, fromUser) {
    target = isMaster ? 1 : 0;
    if (fromUser) auto = false;
    abBtns.forEach(function (b) { b.setAttribute("aria-pressed", String((b.dataset.state === "master") === isMaster)); });
    var r = isMaster ? readings.master : readings.mix;
    chain.forEach(function (li, idx) { li.classList.toggle("on", idx < r.chain); });
    if (stateLabel) stateLabel.textContent = isMaster ? stateLabel.dataset.master : stateLabel.dataset.mix;
    if (lufsFill) lufsFill.style.width = (isMaster ? 86 : 38) + "%";
    tweenNum(outLufs, r.lufs, 1, " LUFS");
    tweenNum(outTp, r.tp, 1, " dBTP");
    tweenNum(outDr, r.dr, 0, " dB");
    tweenNum(outW, r.width, 0, "%");
    if (outTp) outTp.classList.toggle("good", isMaster);
    if (outLufs) outLufs.classList.toggle("good", isMaster);
  }

  function tweenNum(el, to, dec, unit) {
    if (!el) return;
    var from = parseFloat(el.dataset.v || to);
    var t0 = performance.now(), dur = reduce ? 1 : 900;
    (function step(now) {
      var k = Math.min((now - t0) / dur, 1), e = 1 - Math.pow(1 - k, 3);
      var v = from + (to - from) * e;
      el.textContent = (v > 0 && unit.indexOf("TP") > -1 ? "+" : "") + v.toFixed(dec) + unit;
      if (k < 1) requestAnimationFrame(step); else el.dataset.v = to;
    })(t0);
  }

  function buildPath(sign, t) {
    var step = W / (N - 1), d = "M0 " + MID;
    for (var i = 0; i < N; i++) {
      var j = (i + Math.floor(offset)) % N;
      var a = before[j] + (after[j] - before[j]) * mix;
      var wob = reduce ? 1 : (0.9 + 0.1 * Math.sin(t / 180 + i * 0.7));
      var y = MID - sign * a * wob * (MID - 8);
      d += " L" + (i * step).toFixed(1) + " " + y.toFixed(1);
    }
    return d + " L" + W + " " + MID + " Z";
  }

  function frame(t) {
    mix += (target - mix) * 0.06;
    if (!reduce) offset += 0.35;
    if (wavePath) wavePath.setAttribute("d", buildPath(1, t));
    if (wavePath2) wavePath2.setAttribute("d", buildPath(-1, t));
    bars.forEach(function (b, k) {
      var base = specBefore[k] + (specAfter[k] - specBefore[k]) * mix;
      var jitter = reduce ? 0 : (Math.sin(t / 140 + k * 1.7) + Math.sin(t / 97 + k)) * 0.06;
      b.style.height = Math.max(6, Math.min(100, (base + jitter) * 100)) + "%";
    });
    if (auto && t - lastSwitch > 4200) { lastSwitch = t; setState(target === 0, false); }
    if (!reduce || Math.abs(target - mix) > 0.001) requestAnimationFrame(frame);
  }

  abBtns.forEach(function (b) {
    b.addEventListener("click", function () { setState(b.dataset.state === "master", true); if (reduce) requestAnimationFrame(frame); });
  });

  // Pause animation work when offscreen
  var running = false;
  function start() { if (!running) { running = true; requestAnimationFrame(frame); } }
  if ("IntersectionObserver" in window) {
    new IntersectionObserver(function (e) {
      if (e[0].isIntersecting) { start(); } else { running = false; }
    }).observe(consoleEl);
    var origFrame = frame;
    frame = function (t) { if (!running) return; origFrame(t); };
  }
  setState(false, false);
  start();
  if (reduce) setState(true, false);
})();
