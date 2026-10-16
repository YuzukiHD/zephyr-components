// Benchmark of the renderer: a fixed list of scenes, each one a single kind of work, played one
// after the other. A scene change is logged as `BENCH <n> <name>`; the player prints its own
// statistics every 5 s, so a scene lasts SCENE_TICKS (long enough for one full report window).
// Every moving value comes from one frame counter, so what a scene costs does not depend on
// input or on real time: the same ticks give the same pictures.

import { For, Match, Switch, createSignal } from "solid-js";
import { Image, Text, View } from "@pocketjs/framework/components";
import { onButtonPress, onFrame } from "@pocketjs/framework/lifecycle";
import { BTN } from "@pocketjs/framework/input";

const SCENE_TICKS = 360;
// the name of a scene to show for ever (to look at the screen), "" runs the benchmark
const LOOK: string = "";
// keys on the console: d / a = next / previous scene (the rotation stops), j = rotation on/off
const SCENES = ["idle", "fill", "round", "alpha", "text", "rotate", "image", "mixed", "spin", "calls", "objs", "scroll", "stretch", "zoom", "move", "kf", "still"];

const range = (n: number) => Array.from({ length: n }, (_, i) => i);
const wave = (t: number, period: number, phase = 0) =>
  Math.sin(((t + phase) * 2 * Math.PI) / period);

// Calibration of the script engine: the work of a frame is plain script, nothing is drawn
let sink = 0;
function spin() {
  let x = 0;
  for (let i = 0; i < 50000; i++) x = (x + i * 3) | 0;
  sink = x;
}
function trivial(a: number) {
  return a + 1;
}
function calls() {
  let x = 0;
  for (let i = 0; i < 10000; i++) x = trivial(x);
  sink = x;
}
function objs() {
  const list: { x: number; y: number }[] = [];
  for (let i = 0; i < 2000; i++) list.push({ x: i, y: i * 2 });
  sink = list.length;
}

const [tick, setTick] = createSignal(0);
const [scene, setScene] = createSignal(0);

/** Opaque rectangles that overlap and slide: fill rate and overdraw of the plain fill path. */
function Fill() {
  const N = 8;
  return (
    <For each={range(N)}>
      {(i) => (
        <View
          class="absolute left-0 top-0 w-[320] h-[220] bg-[#3b82f6]"
          style={{
            translateX: 90 + 90 * wave(tick(), 120 + i * 9, i * 17),
            translateY: 60 + 50 * wave(tick(), 90 + i * 7, i * 11),
            opacity: 1,
          }}
        />
      )}
    </For>
  );
}

/** Rounded rectangles: the anti-aliased corner path, no blending over the neighbours. */
function Round() {
  const N = 40;
  return (
    <For each={range(N)}>
      {(i) => (
        <View
          class="absolute left-0 top-0 w-[96] h-[64] rounded-[14px] bg-[#f59e0b]"
          style={{
            translateX: 200 + 200 * wave(tick(), 150 + (i % 7) * 11, i * 13),
            translateY: 150 + 130 * wave(tick(), 110 + (i % 5) * 13, i * 29),
          }}
        />
      )}
    </For>
  );
}

/** Half transparent layers: every pixel is read, blended and written. */
function Alpha() {
  const N = 12;
  return (
    <For each={range(N)}>
      {(i) => (
        <View
          class="absolute left-0 top-0 w-[220] h-[160] rounded-[18px] bg-[#ec4899]"
          style={{
            translateX: 140 + 120 * wave(tick(), 160 + i * 10, i * 19),
            translateY: 100 + 90 * wave(tick(), 130 + i * 8, i * 23),
            opacity: 0.45,
          }}
        />
      )}
    </For>
  );
}

/** Text that changes every tick: glyph layout and the coverage (A8) blit. */
function Labels() {
  const ROWS = range(16);
  const COLS = range(4);
  return (
    <View class="absolute left-[8] top-[8] flex-col gap-[2]">
      <For each={ROWS}>
        {(r) => (
          <View class="flex-row gap-[14]">
            <For each={COLS}>
              {(c) => (
                <Text class="w-[116] text-sm text-white">
                  {`r${r}c${c} ${(tick() * (r + 1) + c * 977) % 100000}`}
                </Text>
              )}
            </For>
          </View>
        )}
      </For>
    </View>
  );
}

/** Rotating squares: the transformed, bilinear sampling path. */
function Rotate() {
  const N = 24;
  return (
    <For each={range(N)}>
      {(i) => (
        <View
          class="absolute left-0 top-0 w-[84] h-[84] rounded-[10px] bg-[#10b981]"
          style={{
            translateX: 30 + (i % 6) * 80,
            translateY: 20 + Math.floor(i / 6) * 86,
            rotate: (tick() * (2 + (i % 5)) + i * 15) % 360,
            scale: 0.8 + 0.2 * wave(tick(), 100, i * 9),
          }}
        />
      )}
    </For>
  );
}

/** Scaled and rotated textures (SVG icons rasterised once, then sampled each frame). */
function Icons() {
  const N = 28;
  return (
    <For each={range(N)}>
      {(i) => (
        <View
          class="absolute left-0 top-0 w-[64] h-[64]"
          style={{
            translateX: 20 + (i % 7) * 70,
            translateY: 20 + Math.floor(i / 7) * 88,
            rotate: (tick() * 3 + i * 21) % 360,
            scale: 0.9 + 0.5 * wave(tick(), 120, i * 7),
          }}
        >
          <Image class="w-[64] h-[64]" src="bench-icon.svg" />
        </View>
      )}
    </For>
  );
}

/** A little of everything, like a real screen. */
function Mixed() {
  return (
    <>
      <Fill />
      <Round />
      <Alpha />
      <View class="absolute left-[8] top-[8] flex-col gap-[2]">
        <For each={range(6)}>
          {(r) => <Text class="text-sm text-white">{`frame ${tick()} row ${r}`}</Text>}
        </For>
      </View>
    </>
  );
}


const ROW_COUNT = 80;
const ROWS = range(ROW_COUNT);

/** One row of a list: a dot and a line of text */
function ListRows() {
  return (
    <For each={ROWS}>
      {(r) => (
        <View class="w-[480] h-[24] flex-row items-center px-[8] gap-[8]">
          <View class="w-[14] h-[14] rounded-full bg-[#38bdf8]" />
          <Text class="text-sm text-white">{`Row ${r}  a list item that scrolls by`}</Text>
        </View>
      )}
    </For>
  );
}

/** A long list moved up and down inside a clipped view: one style write moves 80 rows */
function Scroll() {
  const span = ROW_COUNT * 24 - 320 > 0 ? ROW_COUNT * 24 - 320 : 1;
  // a triangle wave: down, then up again
  const y = () => {
    const p = (tick() * 6) % (2 * span);
    return -(p < span ? p : 2 * span - p);
  };
  return (
    <View class="absolute left-[16] top-[32] w-[480] h-[320] overflow-hidden rounded-[8px] bg-[#1e293b]">
      <View class="absolute left-0 top-0 w-[480] flex-col" style={{ translateY: y() }}>
        <ListRows />
      </View>
    </View>
  );
}

/** Boxes and a text stretched differently in x and y */
function Stretch() {
  return (
    <>
      <For each={range(12)}>
        {(i) => (
          <View
            class="absolute left-0 top-0 w-[80] h-[50] rounded-[8px] bg-[#f97316]"
            style={{
              translateX: 24 + (i % 4) * 120,
              translateY: 40 + Math.floor(i / 4) * 90,
              scaleX: 1 + 0.7 * wave(tick(), 90, i * 8),
              scaleY: 1 - 0.55 * wave(tick(), 90, i * 8),
            }}
          />
        )}
      </For>
      <View
        class="absolute left-[130] top-[320]"
        style={{
          scaleX: 1.6 + 0.9 * wave(tick(), 120),
          scaleY: 1.0 - 0.4 * wave(tick(), 120),
        }}
      >
        <Text class="text-2xl text-white font-bold">STRETCH</Text>
      </View>
    </>
  );
}

/** A group of shapes, text and an icon that grows and shrinks around its centre */
function Zoom() {
  return (
    <>
      <View
        class="absolute left-[156] top-[132] w-[200] h-[120]"
        style={{ originX: 0.5, originY: 0.5, scale: 1.2 + 1.0 * wave(tick(), 180) }}
      >
        <View class="absolute left-0 top-0 w-[200] h-[120] rounded-[16px] bg-[#6366f1]" />
        <Text class="absolute left-[16] top-[12] text-xl text-white font-bold">ZOOM</Text>
        <Image class="absolute left-[120] top-[30] w-[64] h-[64]" src="bench-icon.svg" />
      </View>
      <For each={range(6)}>
        {(i) => (
          <View
            class="absolute left-0 top-0 w-[40] h-[40] rounded-full bg-[#22c55e]"
            style={{
              originX: 0.5,
              originY: 0.5,
              translateX: 30 + i * 80,
              translateY: 330,
              scale: 1 + 0.9 * wave(tick(), 100, i * 15),
            }}
          />
        )}
      </For>
    </>
  );
}

/** Icons and rounded boxes that move over the screen, nothing is scaled or rotated */
function Move() {
  return (
    <>
      <For each={range(24)}>
        {(i) => (
          <Image
            class="absolute left-0 top-0 w-[32] h-[32]"
            src="bench-icon.svg"
            style={{
              translateX: 240 + 220 * wave(tick(), 140 + (i % 5) * 17, i * 11),
              translateY: 190 + 160 * wave(tick(), 110 + (i % 7) * 13, i * 19),
            }}
          />
        )}
      </For>
      <For each={range(16)}>
        {(i) => (
          <View
            class="absolute left-0 top-0 w-[48] h-[32] rounded-[6px] bg-[#e11d48]"
            style={{
              translateX: 240 + 200 * wave(tick(), 170 + (i % 4) * 21, i * 23),
              translateY: 190 + 150 * wave(tick(), 130 + (i % 6) * 9, i * 7),
            }}
          />
        )}
      </For>
    </>
  );
}

/** The same four effects driven by keyframes in the engine: no script runs per frame */
function Keyframes() {
  return (
    <>
      <View class="absolute left-[16] top-[16] w-[240] h-[170] overflow-hidden rounded-[8px] bg-[#1e293b]">
        <View class="absolute left-0 top-0 w-[240] flex-col animate-kf-scroll">
          <ListRows />
        </View>
      </View>
      <For each={range(4)}>
        {(i) => (
          <View class="absolute left-0 top-0" style={{ translateX: 280 + i * 56, translateY: 16 }}>
            <View class="w-[40] h-[40] rounded-[6px] bg-[#f97316] animate-kf-stretch" />
          </View>
        )}
      </For>
      <View class="absolute left-[330] top-[110] w-[120] h-[70] animate-kf-zoom">
        <View class="absolute left-0 top-0 w-[120] h-[70] rounded-[12px] bg-[#6366f1]" />
        <Text class="absolute left-[10] top-[8] text-base text-white font-bold">ZOOM</Text>
      </View>
      <For each={range(8)}>
        {(i) => (
          <View class="absolute left-0 top-0" style={{ translateX: 16, translateY: 210 + i * 20 }}>
            <Image class="w-[32] h-[32] animate-kf-move" src="bench-icon.svg" />
          </View>
        )}
      </For>
    </>
  );
}

/** A picture that does not move: sharpness of text, hair lines and a 2 pixel checker board. */
function Still() {
  return (
    <>
      <View class="absolute left-[16] top-[12] flex-col gap-[2]">
        <Text class="text-2xl text-white font-bold">Sharpness test 0123456789</Text>
        <Text class="text-base text-white">The quick brown fox jumps over the lazy dog</Text>
        <Text class="text-xs text-white">The quick brown fox jumps over the lazy dog</Text>
      </View>
      <For each={range(8)}>
        {(i) => (
          <View
            class="absolute left-0 top-0 w-[480] h-[1] bg-[#ffffff]"
            style={{ translateX: 16, translateY: 110 + i * 8 }}
          />
        )}
      </For>
      <For each={range(8)}>
        {(r) => (
          <For each={range(24)}>
            {(c) => (
              <View
                class="absolute left-0 top-0 w-[2] h-[2] bg-[#ffffff]"
                style={{ translateX: 16 + c * 4 + (r % 2) * 2, translateY: 190 + r * 2 }}
              />
            )}
          </For>
        )}
      </For>
      <View class="absolute left-[200] top-[250] w-[200] h-[90] rounded-[14px] bg-[#3b82f6]" />
      <View class="absolute left-[16] top-[350] w-[60] h-[30] bg-[#ff0000]" />
      <View class="absolute left-[76] top-[350] w-[60] h-[30] bg-[#00ff00]" />
      <View class="absolute left-[136] top-[350] w-[60] h-[30] bg-[#0000ff]" />
      <View class="absolute left-[196] top-[350] w-[60] h-[30] bg-[#ffffff]" />
      <View class="absolute left-[256] top-[350] w-[60] h-[30] bg-[#000000]" />
      <View class="absolute left-[316] top-[350] w-[60] h-[30] bg-[#ffff00]" />
      <View class="absolute left-[376] top-[350] w-[60] h-[30] bg-[#00ffff]" />
      <View class="absolute left-[436] top-[350] w-[60] h-[30] bg-[#ff00ff]" />
      <View class="absolute left-[220] top-[270] w-[160] h-[50] rounded-[10px] bg-[#0f172a]" />
    </>
  );
}

export default function Bench() {
  let auto = LOOK === "";
  let manual = LOOK === "" ? 0 : Math.max(0, SCENES.indexOf(LOOK));

  const pick = (n: number) => {
    manual = (n + SCENES.length) % SCENES.length;
    auto = false;
  };
  onButtonPress(BTN.RIGHT, () => pick(scene() + 1));
  onButtonPress(BTN.LEFT, () => pick(scene() - 1));
  onButtonPress(BTN.CROSS, () => {
    auto = !auto;
    // the rotation goes on from the scene that is shown
    base = tick() - scene() * SCENE_TICKS;
  });
  let base = 0;

  onFrame(() => {
    const t = tick() + 1;
    const s = auto
      ? Math.min(Math.floor((t - base) / SCENE_TICKS), SCENES.length - 1)
      : manual;
    const name = SCENES[s];

    if (name === "spin") spin();
    else if (name === "calls") calls();
    else if (name === "objs") objs();
    setTick(t);
    if (s !== scene() || t === 1) {
      setScene(s);
      console.log(`BENCH ${s} ${name} tick ${t}`);
    }
  });

  const is = (name: string) => SCENES[scene()] === name;
  return (
    <View debugName="Bench" class="w-full h-full bg-[#0f172a]">
      <Switch>
        <Match when={is("fill")}>
          <Fill />
        </Match>
        <Match when={is("round")}>
          <Round />
        </Match>
        <Match when={is("alpha")}>
          <Alpha />
        </Match>
        <Match when={is("text")}>
          <Labels />
        </Match>
        <Match when={is("rotate")}>
          <Rotate />
        </Match>
        <Match when={is("image")}>
          <Icons />
        </Match>
        <Match when={is("mixed")}>
          <Mixed />
        </Match>
        <Match when={is("scroll")}>
          <Scroll />
        </Match>
        <Match when={is("stretch")}>
          <Stretch />
        </Match>
        <Match when={is("zoom")}>
          <Zoom />
        </Match>
        <Match when={is("move")}>
          <Move />
        </Match>
        <Match when={is("kf")}>
          <Keyframes />
        </Match>
        <Match when={is("still")}>
          <Still />
        </Match>
      </Switch>
    </View>
  );
}
