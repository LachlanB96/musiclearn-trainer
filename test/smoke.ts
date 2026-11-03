// smoke.ts: the check's smoke test of a build's Standalone. check.ts runs it inside warden-run:
//
//   bun test/smoke.ts <build folder>
//
// It opens the Standalone without focus on the right-most screen (start-standalone.ps1) with a fresh data folder, so
// it loads the UI built into it and leaves the real WebView2 profile alone. Then it asks the page, through WebView2's
// remote debugging port (the Chrome DevTools Protocol), whether it works as the plugin's page: served by the editor's
// resource provider, given the JUCE bridge's initialisation data, its tabs built, the Bravura font loaded, the
// editor's transport events arriving (C++ to page), a resize the page asks for reaching the window (page to C++), and
// no exception or console error while it reloads. It saves a screenshot of the page and exits 1 naming what failed.

import { at, pwsh } from "megaproject";
import { existsSync, mkdirSync, rmSync, writeFileSync } from "fs";
import path from "path";

/** Where JUCE's resource provider serves the page from on Windows (WebBrowserComponent::getResourceProviderRoot) */
export const RESOURCE_ROOT = "https://juce.backend/";

/** The editor's timer sends a transport event 60 times a second; fewer than this in the probe's second is broken */
export const MIN_TRANSPORT_EVENTS = 20;

/** What the page says about itself: PROBE's answer */
export type Probe = {
  href: string;
  host: string;
  ui: string;
  tabs: number;
  bravura: boolean;
  transport: number;
  width: number;
  asked: number;
};

/** Run in the page. It asks the editor for a new width through the bridge and counts transport events meanwhile. */
export const PROBE = `(async () => {
  const juce = window.__JUCE__;
  const first = v => Array.isArray(v) ? v[0] : v;
  await document.fonts.ready;
  const bravura = await document.fonts.load('40px Bravura').then(f => f.length > 0, () => false);
  let transport = 0;
  if (juce) juce.backend.addEventListener('transport', () => { transport++; });
  const asked = window.innerWidth === 760 ? 800 : 760;
  if (juce) juce.backend.emitEvent('ui', { type: 'resize', w: asked, h: 520 });
  await new Promise(done => setTimeout(done, 1000));
  return {
    href: location.href,
    host: String(juce ? first(juce.initialisationData.host) : 'no window.__JUCE__'),
    ui: String(juce ? first(juce.initialisationData.ui) : 'no window.__JUCE__'),
    tabs: document.querySelectorAll('#tabs button').length,
    bravura, transport, width: window.innerWidth, asked,
  };
})()`;

/** What's wrong, from the probe's answer and the errors the page reported while it reloaded; empty when it passed */
export function problems(p: Probe, errors: string[]): string[] {
  const out: string[] = [];
  if (!p.href.startsWith(RESOURCE_ROOT)) out.push(`the page is ${p.href}, not the one the editor serves (${RESOURCE_ROOT})`);
  if (p.host !== "plugin") out.push(`the page's host is "${p.host}", not "plugin": the JUCE bridge's initialisation data didn't arrive`);
  if (p.ui !== "embedded") out.push(`the page came from the "${p.ui}" UI, not the one built in: the fresh data folder wasn't used`);
  if (p.tabs < 1) out.push("the page has no tabs: its script didn't build the UI");
  if (!p.bravura) out.push("the Bravura font didn't load from the resource provider");
  if (p.transport < MIN_TRANSPORT_EVENTS) {
    out.push(`${p.transport} transport events in a second, where the editor sends 60: the editor's events don't reach the page`);
  }
  if (Math.abs(p.width - p.asked) > 2) {
    out.push(`the page asked the editor for ${p.asked} px wide and is ${p.width}: the page's events don't reach the editor`);
  }
  for (const e of errors) out.push(`the page reported: ${e}`);
  return out;
}

/** One entry of the debugging port's /json/list */
export type Target = { type: string; url: string; webSocketDebuggerUrl?: string };

/** The page the Standalone serves its UI in, from /json/list, or null while it isn't there yet */
export function pageTarget(list: Target[]): Target | null {
  return list.find(t => t.type === "page" && t.url.startsWith(RESOURCE_ROOT) && !!t.webSocketDebuggerUrl) ?? null;
}

type Json = Record<string, unknown>;
const obj = (v: unknown): Json => (v && typeof v === "object" ? (v as Json) : {});
const str = (v: unknown): string => (typeof v === "string" ? v : v === undefined ? "" : JSON.stringify(v));

/** The text of an error the page reported, from a DevTools event, or null when the event isn't an error */
export function errorText(method: string, params: Json): string | null {
  if (method === "Runtime.exceptionThrown") {
    const d = obj(params.exceptionDetails);
    const where = d.url ? ` (${str(d.url)}:${str(d.lineNumber)})` : "";
    return `uncaught ${str(obj(d.exception).description) || str(d.text)}${where}`;
  }
  if (method === "Runtime.consoleAPICalled" && params.type === "error") {
    const args = Array.isArray(params.args) ? params.args : [];
    return `console.error ${args.map(a => str(obj(a).value ?? obj(a).description)).join(" ")}`;
  }
  if (method === "Log.entryAdded") {
    const e = obj(params.entry);
    if (e.level !== "error") return null;
    return `${str(e.source)} error ${str(e.text)}${e.url ? ` (${str(e.url)})` : ""}`;
  }
  return null;
}

/** A DevTools Protocol connection to one page */
class Cdp {
  private readonly ws: WebSocket;
  private nextId = 1;
  private readonly pending = new Map<number, { resolve: (v: Json) => void; reject: (e: Error) => void }>();
  private readonly handlers: ((method: string, params: Json) => void)[] = [];

  private constructor(ws: WebSocket) {
    this.ws = ws;
    ws.addEventListener("message", ev => {
      const m = obj(JSON.parse(String(ev.data)));
      if (typeof m.id === "number") {
        const waiting = this.pending.get(m.id);
        if (!waiting) return;
        this.pending.delete(m.id);
        if (m.error) waiting.reject(new Error(str(obj(m.error).message)));
        else waiting.resolve(obj(m.result));
      } else if (typeof m.method === "string") {
        for (const h of this.handlers) h(m.method, obj(m.params));
      }
    });
    ws.addEventListener("close", () => {
      for (const w of this.pending.values()) w.reject(new Error("the page's debugging connection closed"));
      this.pending.clear();
    });
  }

  static open(url: string): Promise<Cdp> {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(url);
      ws.addEventListener("open", () => resolve(new Cdp(ws)), { once: true });
      ws.addEventListener("error", () => reject(new Error(`couldn't connect to the page at ${url}`)), { once: true });
    });
  }

  send(method: string, params: Json = {}): Promise<Json> {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }

  on(handler: (method: string, params: Json) => void): void {
    this.handlers.push(handler);
  }

  /** The next event called method, within ms */
  nextEvent(method: string, ms: number): Promise<Json> {
    return within(new Promise(resolve => this.on((m, p) => { if (m === method) resolve(p); })), ms, method);
  }

  close(): void {
    this.ws.close();
  }
}

function within<T>(p: Promise<T>, ms: number, what: string): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const late = new Promise<never>((_, reject) => { timer = setTimeout(() => reject(new Error(`no ${what} in ${ms / 1000} s`)), ms); });
  return Promise.race([p, late]).finally(() => clearTimeout(timer));
}

function freePort(): number {
  const server = Bun.listen({ hostname: "127.0.0.1", port: 0, socket: { data() {} } });
  const port = server.port;
  server.stop(true);
  return port;
}

/** Waits for the Standalone's page to show up on the debugging port */
async function waitForPage(port: number, ms: number): Promise<Target> {
  const until = Date.now() + ms;
  let last = "nothing answered";
  while (Date.now() < until) {
    try {
      const res = await fetch(`http://127.0.0.1:${port}/json/list`);
      const target = pageTarget((await res.json()) as Target[]);
      if (target) return target;
      last = "the debugging port answered without the Trainer's page";
    } catch (e) {
      last = `the debugging port didn't answer (${(e as Error).message})`; // WebView2 is still starting
    }
    await Bun.sleep(250);
  }
  throw new Error(`the Standalone's page didn't come up in ${ms / 1000} s: ${last}`);
}

const say = (text: string) => console.log(`[smoke] ${text}`);

export async function main(argv: string[]): Promise<number> {
  const build = argv[0];
  if (!build) throw new Error("usage: bun test/smoke.ts <build folder>");
  const exe = path.join(build, "MusicLearnTrainer_artefacts", "Release", "Standalone", "MusicLearn Trainer.exe");
  if (!existsSync(exe)) throw new Error(`no Standalone at ${exe}: build it first`);
  const local = process.env.LOCALAPPDATA;
  if (!local) throw new Error("LOCALAPPDATA isn't set");
  const work = path.join(local, "MusicLearnTrainer", "check");
  const data = path.join(work, "data");
  rmSync(data, { recursive: true, force: true });
  mkdirSync(data, { recursive: true });

  const port = freePort();
  const script = path.join(import.meta.dir, "start-standalone.ps1");
  const start = Bun.spawnSync(
    [pwsh, "-NoProfile", "-File", script, "-StartUnfocused", at("@LBTerm:start-unfocused"), "-Exe", exe, "-Data", data, "-Port", String(port)],
    { stdout: "pipe", stderr: "pipe" },
  );
  const said = start.stdout.toString().trim();
  if (start.exitCode !== 0) throw new Error(`start-standalone.ps1 failed (exit ${start.exitCode}): ${start.stderr.toString().trim() || said}`);
  const pid = Number(said.split(/\r?\n/).pop());
  if (!Number.isInteger(pid) || pid <= 0) throw new Error(`start-standalone.ps1 printed no process id: ${said}`);
  say(`Standalone ${exe} (pid ${pid}), data ${data}, debugging port ${port}`);

  let cdp: Cdp | undefined;
  try {
    const target = await waitForPage(port, 20_000);
    cdp = await Cdp.open(target.webSocketDebuggerUrl!);
    const errors: string[] = [];
    cdp.on((method, params) => { const e = errorText(method, params); if (e) errors.push(e); });
    await cdp.send("Runtime.enable");
    await cdp.send("Log.enable");
    await cdp.send("Page.enable");
    // Reload with the listeners on, so whatever the page reports while it starts is heard
    const loaded = cdp.nextEvent("Page.loadEventFired", 15_000);
    await cdp.send("Page.reload", { ignoreCache: true });
    await loaded;
    const answer = await cdp.send("Runtime.evaluate", { expression: PROBE, awaitPromise: true, returnByValue: true, timeout: 15_000 });
    if (answer.exceptionDetails) {
      const d = obj(answer.exceptionDetails);
      throw new Error(`the probe failed in the page: ${str(obj(d.exception).description) || str(d.text)}`);
    }
    const probe = obj(answer.result).value as Probe;
    const shot = await cdp.send("Page.captureScreenshot", { format: "png" });
    const png = path.join(work, "standalone.png");
    writeFileSync(png, Buffer.from(str(shot.data), "base64"));

    say(`page ${probe.href}: host ${probe.host}, ${probe.ui} UI, ${probe.tabs} tabs, Bravura ${probe.bravura ? "loaded" : "missing"}, ` +
      `${probe.transport} transport events in 1 s, asked for ${probe.asked} px wide and is ${probe.width}`);
    say(`screenshot ${png}`);
    const wrong = problems(probe, errors);
    for (const w of wrong) say(`FAILED: ${w}`);
    say(wrong.length ? `${wrong.length} problem${wrong.length === 1 ? "" : "s"}` : "passed");
    return wrong.length ? 1 : 0;
  } finally {
    cdp?.close();
    try {
      process.kill(pid);
    } catch {
      // it has already ended
    }
  }
}

if (import.meta.main) {
  main(process.argv.slice(2)).then(
    code => process.exit(code),
    e => { console.error(`[smoke] FAILED: ${(e as Error).message}`); process.exit(1); },
  );
}
