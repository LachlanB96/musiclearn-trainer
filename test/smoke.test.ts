import { describe, expect, test } from "bun:test";
import { errorText, MIN_TRANSPORT_EVENTS, pageTarget, problems, type Probe } from "./smoke.ts";

const good: Probe = {
  href: "https://juce.backend/",
  host: "plugin",
  ui: "embedded",
  tabs: 5,
  bravura: true,
  transport: 58,
  width: 760,
  asked: 760,
};

describe("problems", () => {
  test("none for a page that works", () => {
    expect(problems(good, [])).toEqual([]);
  });

  test("a page that isn't the editor's", () => {
    expect(problems({ ...good, href: "about:blank" }, [])).toEqual(["the page is about:blank, not the one the editor serves (https://juce.backend/)"]);
  });

  test("no bridge, and the live UI where the built-in one was wanted", () => {
    const wrong = problems({ ...good, host: "no window.__JUCE__", ui: "live" }, []);
    expect(wrong).toHaveLength(2);
    expect(wrong[0]).toContain("initialisation data didn't arrive");
    expect(wrong[1]).toContain("the fresh data folder wasn't used");
  });

  test("no tabs and no font", () => {
    expect(problems({ ...good, tabs: 0, bravura: false }, [])).toEqual([
      "the page has no tabs: its script didn't build the UI",
      "the Bravura font didn't load from the resource provider",
    ]);
  });

  test("events that don't arrive either way", () => {
    const wrong = problems({ ...good, transport: MIN_TRANSPORT_EVENTS - 1, width: 722 }, []);
    expect(wrong[0]).toContain("the editor's events don't reach the page");
    expect(wrong[1]).toBe("the page asked the editor for 760 px wide and is 722: the page's events don't reach the editor");
  });

  test("a width within 2 px of the one asked for passes (DPI rounding)", () => {
    expect(problems({ ...good, width: 762 }, [])).toEqual([]);
  });

  test("each error the page reported", () => {
    expect(problems(good, ["uncaught TypeError: x is undefined"])).toEqual(["the page reported: uncaught TypeError: x is undefined"]);
  });
});

describe("pageTarget", () => {
  test("the Trainer's page, not another target and not one without a socket", () => {
    const page = { type: "page", url: "https://juce.backend/", webSocketDebuggerUrl: "ws://127.0.0.1:1/devtools/page/A" };
    expect(pageTarget([
      { type: "service_worker", url: "https://juce.backend/sw.js", webSocketDebuggerUrl: "ws://x" },
      { type: "page", url: "https://juce.backend/" },
      page,
    ])).toBe(page);
  });

  test("null while the page isn't up", () => {
    expect(pageTarget([{ type: "page", url: "about:blank", webSocketDebuggerUrl: "ws://x" }])).toBeNull();
  });
});

describe("errorText", () => {
  test("an uncaught exception, with where", () => {
    const params = { exceptionDetails: { text: "Uncaught", url: "https://juce.backend/", lineNumber: 812, exception: { description: "TypeError: S.song is null" } } };
    expect(errorText("Runtime.exceptionThrown", params)).toBe("uncaught TypeError: S.song is null (https://juce.backend/:812)");
  });

  test("console.error, but not console.log", () => {
    expect(errorText("Runtime.consoleAPICalled", { type: "error", args: [{ value: "no song" }, { value: 3 }] })).toBe("console.error no song 3");
    expect(errorText("Runtime.consoleAPICalled", { type: "log", args: [{ value: "hi" }] })).toBeNull();
  });

  test("a browser log error, such as a resource that didn't load", () => {
    const params = { entry: { level: "error", source: "network", text: "Failed to load resource: 404", url: "https://juce.backend/Bravura.woff2" } };
    expect(errorText("Log.entryAdded", params)).toBe("network error Failed to load resource: 404 (https://juce.backend/Bravura.woff2)");
    expect(errorText("Log.entryAdded", { entry: { level: "warning", text: "slow" } })).toBeNull();
  });

  test("other events aren't errors", () => {
    expect(errorText("Page.loadEventFired", { timestamp: 1 })).toBeNull();
  });
});
