// @ts-check
/**
 * Comparador ANTES / DESPUÉS con Web Audio: las dos versiones suenan sincronizadas
 * y el cambio es instantáneo (crossfade de 30 ms), sin autoplay. Los archivos solo
 * se descargan cuando el usuario pulsa reproducir. Ajuste de nivel opcional
 * (data-gain-a / data-gain-b en dB) para comparar a volumen igualado.
 */

/** @param {number} s */
const mmss = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, "0")}`;
const dbToGain = (/** @type {number} */ db) => Math.pow(10, db / 20);

/** @type {AudioContext|null} */
let sharedCtx = null;
/** @type {(() => void)|null} */
let stopOther = null;

/** @param {HTMLElement} root */
export function initAB(root) {
  const playBtn = /** @type {HTMLButtonElement} */ (root.querySelector(".ab-play"));
  const seek = /** @type {HTMLInputElement} */ (root.querySelector(".ab-wave input"));
  const canvas = /** @type {HTMLCanvasElement} */ (root.querySelector(".ab-wave canvas"));
  const time = /** @type {HTMLElement} */ (root.querySelector(".ab-time"));
  const switches = /** @type {NodeListOf<HTMLButtonElement>} */ (root.querySelectorAll(".ab-switch button"));
  const labels = { play: playBtn.getAttribute("aria-label") || "Play", pause: root.dataset.pauseLabel || "Pause", loading: root.dataset.loadingLabel || "…" };
  const ctx2d = canvas.getContext("2d");
  const icon = playBtn.querySelector("use");
  const setIcon = (/** @type {boolean} */ on) => icon?.setAttribute("href", on ? "#i-pause" : "#i-play");

  /** @type {AudioBuffer|null} */ let bufA = null;
  /** @type {AudioBuffer|null} */ let bufB = null;
  /** @type {AudioBufferSourceNode[]} */ let sources = [];
  /** @type {GainNode|null} */ let gA = null;
  /** @type {GainNode|null} */ let gB = null;
  let version = "a";
  let playing = false;
  let offset = 0;
  let startedAt = 0;
  /** @type {Float32Array|null} */ let peaksA = null;
  /** @type {Float32Array|null} */ let peaksB = null;

  const duration = () => (bufA ? Math.min(bufA.duration, bufB ? bufB.duration : Infinity) : 0);
  const position = () => (playing && sharedCtx ? Math.min(offset + sharedCtx.currentTime - startedAt, duration()) : offset);

  /** @param {AudioBuffer} buf */
  function peaks(buf, bins = 600) {
    const ch = buf.getChannelData(0);
    const out = new Float32Array(bins);
    const step = Math.floor(ch.length / bins) || 1;
    for (let i = 0; i < bins; i++) {
      let max = 0;
      for (let j = i * step, e = Math.min(ch.length, j + step); j < e; j += 8) { const v = Math.abs(ch[j]); if (v > max) max = v; }
      out[i] = max;
    }
    return out;
  }

  function draw() {
    if (!ctx2d) return;
    const dpr = Math.min(devicePixelRatio || 1, 2);
    const w = (canvas.width = Math.round(canvas.clientWidth * dpr));
    const h = (canvas.height = Math.round(canvas.clientHeight * dpr));
    ctx2d.clearRect(0, 0, w, h);
    const p = version === "a" ? peaksA : peaksB;
    const ghost = version === "a" ? peaksB : peaksA;
    const prog = duration() ? position() / duration() : 0;
    const bar = (/** @type {Float32Array} */ arr, /** @type {(x:number)=>string} */ color) => {
      const bw = w / arr.length;
      for (let i = 0; i < arr.length; i++) {
        const x = i * bw, a = Math.max(arr[i] * h * 0.92, dpr);
        ctx2d.fillStyle = color(x / w);
        ctx2d.fillRect(x, (h - a) / 2, Math.max(bw - dpr * 0.6, dpr * 0.6), a);
      }
    };
    if (ghost) bar(ghost, () => "rgba(128,120,120,.25)");
    if (p) bar(p, (fx) => (fx <= prog ? "#e01e2d" : "rgba(128,120,120,.7)"));
    else { ctx2d.fillStyle = "rgba(128,120,120,.35)"; ctx2d.fillRect(0, h / 2 - dpr, w, 2 * dpr); }
  }

  function tick() {
    const d = duration();
    const pos = position();
    time.textContent = `${mmss(pos)} / ${mmss(d)}`;
    seek.value = String(d ? Math.round((pos / d) * 1000) : 0);
    seek.setAttribute("aria-valuetext", `${mmss(pos)} / ${mmss(d)}`);
    draw();
    if (playing) {
      if (pos >= d - 0.02) { stop(); offset = 0; tick(); return; }
      requestAnimationFrame(tick);
    }
  }

  async function load() {
    if (bufA && bufB) return;
    playBtn.setAttribute("aria-label", labels.loading);
    playBtn.disabled = true;
    const AC = window.AudioContext || /** @type {any} */ (window).webkitAudioContext;
    sharedCtx = sharedCtx || new AC();
    const ac = /** @type {AudioContext} */ (sharedCtx);
    const get = async (/** @type {string} */ url) => ac.decodeAudioData(await (await fetch(url)).arrayBuffer());
    try {
      [bufA, bufB] = await Promise.all([get(root.dataset.a || ""), get(root.dataset.b || "")]);
      peaksA = peaks(bufA); peaksB = peaks(bufB);
      const norm = Math.max(...peaksA, ...peaksB) || 1;
      peaksA = peaksA.map((v) => v / norm); peaksB = peaksB.map((v) => v / norm);
    } finally {
      playBtn.disabled = false;
      playBtn.setAttribute("aria-label", labels.play);
    }
  }

  function stop() {
    if (!playing) return;
    offset = position();
    sources.forEach((s) => { try { s.stop(); } catch {} });
    sources = [];
    playing = false;
    playBtn.setAttribute("aria-label", labels.play);
    playBtn.dataset.playing = "false";
    setIcon(false);
  }

  async function play() {
    await load();
    const ac = sharedCtx;
    if (!ac || !bufA || !bufB) return;
    if (ac.state === "suspended") await ac.resume();
    if (stopOther && stopOther !== stop) stopOther();
    stopOther = stop;
    gA = ac.createGain(); gB = ac.createGain();
    const lvA = dbToGain(Number(root.dataset.gainA || 0)), lvB = dbToGain(Number(root.dataset.gainB || 0));
    gA.gain.value = version === "a" ? lvA : 0;
    gB.gain.value = version === "b" ? lvB : 0;
    gA.connect(ac.destination); gB.connect(ac.destination);
    const sA = ac.createBufferSource(); sA.buffer = bufA; sA.connect(gA);
    const sB = ac.createBufferSource(); sB.buffer = bufB; sB.connect(gB);
    const when = ac.currentTime + 0.03;
    sA.start(when, offset); sB.start(when, offset);
    sources = [sA, sB];
    startedAt = when;
    playing = true;
    playBtn.setAttribute("aria-label", labels.pause);
    playBtn.dataset.playing = "true";
    setIcon(true);
    requestAnimationFrame(tick);
  }

  /** @param {string} v */
  function setVersion(v) {
    version = v;
    switches.forEach((b) => b.setAttribute("aria-pressed", String(b.dataset.v === v)));
    if (playing && sharedCtx && gA && gB) {
      const t = sharedCtx.currentTime;
      const lvA = dbToGain(Number(root.dataset.gainA || 0)), lvB = dbToGain(Number(root.dataset.gainB || 0));
      gA.gain.setTargetAtTime(v === "a" ? lvA : 0, t, 0.01);
      gB.gain.setTargetAtTime(v === "b" ? lvB : 0, t, 0.01);
    }
    draw();
  }

  playBtn.addEventListener("click", () => (playing ? stop() : play()));
  switches.forEach((b) => b.addEventListener("click", () => setVersion(b.dataset.v || "a")));
  root.addEventListener("keydown", (e) => {
    if (e.target instanceof HTMLInputElement && e.key !== "a" && e.key !== "b") return;
    if (e.key === "a" || e.key === "b") setVersion(e.key);
  });
  seek.addEventListener("input", async () => {
    const d = duration();
    if (!d) { await load(); }
    const was = playing;
    stop();
    offset = (Number(seek.value) / 1000) * duration();
    if (was) play(); else tick();
  });
  new ResizeObserver(draw).observe(canvas);
  draw();
}
