// Keyframes of the benchmark: the same effects as the scroll / stretch / zoom / move scenes, run
// by the engine (a page-wide `loop` period replays them, no script per frame).

import { definePocketConfig } from "@pocketjs/framework/config";

export default definePocketConfig({
  theme: {
    keyframes: {
      "kf-scroll": { from: { translateY: 0 }, "50%": { translateY: -1750 }, to: { translateY: 0 } },
      "kf-stretch": {
        from: { scaleX: 0.6, scaleY: 1.6 },
        "50%": { scaleX: 1.8, scaleY: 0.5 },
        to: { scaleX: 0.6, scaleY: 1.6 },
      },
      "kf-zoom": { from: { scale: 0.4 }, "50%": { scale: 1.6 }, to: { scale: 0.4 } },
      "kf-move": { from: { translateX: 0 }, "50%": { translateX: 420 }, to: { translateX: 0 } },
    },
    animation: {
      "kf-scroll": { value: "kf-scroll 6s linear both", loop: "6000ms" },
      "kf-stretch": { value: "kf-stretch 3s ease-in-out both", loop: "3000ms" },
      "kf-zoom": { value: "kf-zoom 4s ease-in-out both", loop: "4000ms" },
      "kf-move": { value: "kf-move 5s ease-in-out both", loop: "5000ms" },
    },
  },
});
