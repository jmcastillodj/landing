// @ts-check
/**
 * Instrumento del hero: goniómetro estéreo (vectorscope M/S), correlación de fase,
 * loudness integrado/short-term, true peak y PLR. Muestra cómo cambia una mezcla
 * sin masterizar (más baja, desequilibrada L/R, dinámica irregular) frente al master
 * final (más densa, centrada y por debajo del techo de -1 dBTP).
 * Canvas 2D, sin librerías; se pausa fuera de pantalla y respeta prefers-reduced-motion.
 */

/** Valores de referencia de cada estado (ilustrativos, coherentes con la guía de envío: mezcla a -6 dB de pico). */
const STATES = {
  mix: { lufs: -20.4, tp: -6.1, plr: 14.2, width: 0.24, tilt: 0.22, gain: 0.56, comp: 0 },
  master: { lufs: -9.6, tp: -1.0, plr: 8.6, width: 0.38, tilt: 0, gain: 0.9, comp: 1 },
};
const N = 520; // puntos por fotograma
const HIST = 180; // muestras del histórico de loudness

/** @param {number} a @param {number} b @param {number} t */
const lerp = (a, b, t) => a + (b - a) * t;

/**
 * @param {HTMLElement} root
 * @param {boolean} reduceMotion
 */
export function initScope(root, reduceMotion) {
  const canvas = /** @type {HTMLCanvasElement} */ (root.querySelector(".gonio canvas"));
  const histCanvas = /** @type {HTMLCanvasElement} */ (root.querySelector(".hist canvas"));
  const ctx = canvas.getContext("2d", { alpha: false });
  const hctx = histCanvas.getContext("2d");
  if (!ctx || !hctx) return;
  const buttons = /** @type {NodeListOf<HTMLButtonElement>} */ (root.querySelectorAll(".ab button"));
  const stateLabel = /** @type {HTMLElement|null} */ (root.querySelector("[data-state]"));
  const live = root.querySelector("[data-live]");
  const out = {
    lufs: /** @type {HTMLOutputElement|null} */ (root.querySelector('[data-o="lufs"]')),
    tp: /** @type {HTMLOutputElement|null} */ (root.querySelector('[data-o="tp"]')),
    plr: /** @type {HTMLOutputElement|null} */ (root.querySelector('[data-o="plr"]')),
    corr: /** @type {HTMLOutputElement|null} */ (root.querySelector('[data-o="corr"]')),
  };
  const corrMark = /** @type {HTMLElement|null} */ (root.querySelector(".corr i"));
  const lufsBar = /** @type {HTMLElement|null} */ (root.querySelector(".lufs-track i"));
  const unit = { lufs: " LUFS", tp: " dBTP", plr: " dB" };

  let target = 0; // 0 = mezcla, 1 = master
  let m = 0; // estado interpolado
  let T = 0; // tiempo simulado
  let running = false;
  let visible = true;
  let auto = !reduceMotion;
  let lastSwitch = 0;
  let last = 0;
  let corrSmooth = 0.7;
  let dpr = 1, W = 0, H = 0, hW = 0, hH = 0;
  /** @type {number[]} */
  const hist = new Array(HIST).fill(-24);
  const xs = new Float32Array(N), ys = new Float32Array(N);

  function resize() {
    dpr = Math.min(devicePixelRatio || 1, 2);
    // lecturas primero, escrituras después (sin reflow forzado)
    const cw = canvas.clientWidth, ch = canvas.clientHeight, hw = histCanvas.clientWidth, hh = histCanvas.clientHeight;
    W = canvas.width = Math.round(cw * dpr);
    H = canvas.height = Math.round(ch * dpr);
    hW = histCanvas.width = Math.round(hw * dpr);
    hH = histCanvas.height = Math.round(hh * dpr);
    if (ctx) { ctx.fillStyle = "#0b0a0a"; ctx.fillRect(0, 0, W, H); }
    if (!running) drawStatic();
  }

  /** Genera una ventana de señal estéreo y la proyecta en ejes M/S. Devuelve la correlación. */
  function synth() {
    const s = STATES;
    const width = lerp(s.mix.width, s.master.width, m);
    const tilt = lerp(s.mix.tilt, s.master.tilt, m);
    const gain = lerp(s.mix.gain, s.master.gain, m);
    const comp = lerp(s.mix.comp, s.master.comp, m);
    // envolvente: la mezcla respira más (picos del kick), el master está controlado
    const beat = Math.exp(-((T * 2.1) % 1) * 7);
    const envRaw = 0.45 + 0.55 * Math.pow(Math.abs(Math.sin(T * 0.9)), 2) + 0.35 * beat;
    const env = lerp(envRaw, 0.86 + 0.1 * beat, comp) * gain;
    let sLR = 0, sLL = 0, sRR = 0;
    const r = Math.min(W, H) * 0.46;
    for (let i = 0; i < N; i++) {
      const t = T + i * 0.00042;
      const n1 = Math.sin(t * 9371.3 + Math.sin(t * 131.7) * 4) * 0.12;
      const mono = 0.6 * Math.sin(6.2832 * 110 * t) + 0.26 * Math.sin(6.2832 * 220 * t + 1.3) + 0.14 * Math.sin(6.2832 * 331 * t + 0.4) + n1;
      const side = 0.55 * Math.sin(6.2832 * 165 * t + 0.7) + 0.3 * Math.sin(6.2832 * 497 * t) + Math.sin(t * 7127.9) * 0.15;
      let L = env * (mono + width * side) * (1 + tilt);
      let R = env * (mono - width * side) * (1 - tilt);
      // limitador suave del master: techo a -1 dBTP (≈0,89)
      const ceil = lerp(1.6, 0.89, comp);
      L = ceil * Math.tanh(L / ceil);
      R = ceil * Math.tanh(R / ceil);
      sLR += L * R; sLL += L * L; sRR += R * R;
      // proyección goniómetro: S horizontal, M vertical
      xs[i] = W / 2 + ((L - R) * 0.7071) * r;
      ys[i] = H / 2 - ((L + R) * 0.7071) * r * 0.62;
    }
    return sLR / Math.sqrt(sLL * sRR + 1e-9);
  }

  function drawTrace(fade = 0.2) {
    if (!ctx) return;
    ctx.globalCompositeOperation = "source-over";
    ctx.fillStyle = `rgba(11,10,10,${fade})`;
    ctx.fillRect(0, 0, W, H);
    ctx.globalCompositeOperation = "lighter";
    ctx.lineWidth = 1.1 * dpr;
    ctx.strokeStyle = "rgba(255,60,60,0.62)";
    ctx.beginPath();
    ctx.moveTo(xs[0], ys[0]);
    for (let i = 1; i < N; i++) ctx.lineTo(xs[i], ys[i]);
    ctx.stroke();
    ctx.globalCompositeOperation = "source-over";
  }

  function drawHist() {
    if (!hctx) return;
    hctx.clearRect(0, 0, hW, hH);
    const y = (/** @type {number} */ v) => hH - ((v + 30) / 26) * hH; // -30..-4 LUFS
    hctx.setLineDash([3 * dpr, 4 * dpr]);
    hctx.strokeStyle = "rgba(52,211,153,.7)";
    hctx.lineWidth = dpr;
    hctx.beginPath(); hctx.moveTo(0, y(-14)); hctx.lineTo(hW, y(-14)); hctx.stroke();
    hctx.setLineDash([]);
    const grad = hctx.createLinearGradient(0, 0, 0, hH);
    grad.addColorStop(0, "rgba(255,45,45,.45)"); grad.addColorStop(1, "rgba(255,45,45,0)");
    hctx.beginPath();
    hctx.moveTo(0, hH);
    for (let i = 0; i < HIST; i++) hctx.lineTo((i / (HIST - 1)) * hW, y(hist[i]));
    hctx.lineTo(hW, hH); hctx.closePath();
    hctx.fillStyle = grad; hctx.fill();
    hctx.beginPath();
    for (let i = 0; i < HIST; i++) { const X = (i / (HIST - 1)) * hW; i ? hctx.lineTo(X, y(hist[i])) : hctx.moveTo(X, y(hist[i])); }
    hctx.strokeStyle = "#ff4545"; hctx.lineWidth = 1.6 * dpr; hctx.stroke();
  }

  /** @param {number} corr */
  function updateMeters(corr) {
    const s = STATES;
    const beat = Math.exp(-((T * 2.1) % 1) * 7);
    const wobble = lerp(3.2, 0.9, m) * (Math.sin(T * 0.9) * 0.6 + beat * 0.8 - 0.4);
    const st = lerp(s.mix.lufs, s.master.lufs, m) + wobble;
    hist.push(st); hist.shift();
    corrSmooth += (corr - corrSmooth) * 0.08;
    if (out.corr) out.corr.textContent = (corrSmooth >= 0 ? "+" : "") + corrSmooth.toFixed(2);
    if (corrMark) corrMark.style.left = `${((corrSmooth + 1) / 2) * 100}%`;
    if (lufsBar) lufsBar.style.height = `${Math.max(4, Math.min(100, ((st + 30) / 26) * 100))}%`;
  }

  function setReadouts() {
    const k = target ? "master" : "mix";
    const v = STATES[k];
    /** @type {("lufs"|"tp"|"plr")[]} */ (["lufs", "tp", "plr"]).forEach((key) => {
      const el = out[key];
      if (!el) return;
      el.textContent = v[key].toFixed(1) + unit[key];
      el.classList.toggle("good", !!target && key !== "plr");
    });
  }

  /** @param {number} next @param {boolean} fromUser */
  function setState(next, fromUser) {
    target = next;
    if (fromUser) auto = false;
    buttons.forEach((b) => b.setAttribute("aria-pressed", String((b.dataset.state === "master") === !!next)));
    if (stateLabel) stateLabel.textContent = (next ? stateLabel.dataset.master : stateLabel.dataset.mix) || "";
    if (fromUser && live && stateLabel) live.textContent = stateLabel.textContent;
    setReadouts();
    if (reduceMotion) { m = next; drawStatic(); }
  }

  function drawStatic() {
    if (!ctx || !W) return;
    ctx.fillStyle = "#0b0a0a"; ctx.fillRect(0, 0, W, H);
    for (let k = 0; k < 6; k++) { T += 0.37; drawTrace(0); }
    const c = synth();
    for (let i = 0; i < HIST; i++) { T += 0.05; updateMeters(c); }
    drawHist();
  }

  /** @param {number} now */
  function frame(now) {
    if (!running) return;
    requestAnimationFrame(frame);
    if (now - last < 32) return; // ~30 fps: fluido para un instrumento con estela y barato en móvil
    const dt = Math.min((now - last) / 1000, 0.05);
    last = now;
    T += dt;
    m += (target - m) * 0.1;
    const corr = synth();
    drawTrace();
    updateMeters(corr);
    drawHist();
    if (auto && now - lastSwitch > 4600) { lastSwitch = now; setState(target ? 0 : 1, false); }
  }

  function start() { if (!running && visible && !document.hidden && !reduceMotion) { running = true; last = performance.now(); requestAnimationFrame(frame); } }
  function stop() { running = false; }

  buttons.forEach((b) => b.addEventListener("click", () => setState(b.dataset.state === "master" ? 1 : 0, true)));
  new ResizeObserver(resize).observe(canvas);
  if ("IntersectionObserver" in window) {
    new IntersectionObserver(([e]) => { visible = e.isIntersecting; visible ? start() : stop(); }).observe(root);
  }
  document.addEventListener("visibilitychange", () => (document.hidden ? stop() : start()));
  resize();
  setState(reduceMotion ? 1 : 0, false);
  lastSwitch = performance.now();
  start();
}
