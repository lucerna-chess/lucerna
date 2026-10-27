# Builds docs/logo-calligram.svg: the lamp and the bishop filled with rows of the word LUCERNA,
# and the smoke written as one "Lucerna" along a curve.
LAMP = [
  "M49 96.5 C 39 96.5, 29 92.5, 21.5 83 C 19.5 80.3, 21.6 77.8, 24.4 79.8 C 30.5 84.5, 39 87, 51 87.5 Z",
  "M38 99 C 42.5 89, 57.5 84.5, 74.5 84.5 C 91.5 84.5, 105 90.5, 105 99 C 105 107.5, 91.5 112, 74.5 112 C 58 112, 43.5 108, 38 99 Z",
  "M63.5 85.5 C 63.5 77.5, 85.5 77.5, 85.5 85.5 Z",
  "M102 91 C 108 86.5, 116.5 88, 116.5 95.5 C 116.5 102, 110 106, 103.5 105.5 C 109 104, 112.5 100, 112 95.8 C 111.6 92, 107 90.5, 102.5 93.5 Z",
  "M66 111.5 H 83 C 84 114, 86.5 115.5, 89.5 116.5 H 59.5 C 62.5 115.5, 65 114, 66 111.5 Z",
]
BISHOP = [
  "M72.5 11.5 C 84 18.5, 87.5 28, 82.5 35.5 H 62.5 C 57.5 28, 61 18.5, 72.5 11.5 Z",
  "M61 35.5 H 84 V 40.3 H 61 Z",
  "M67 40.3 H 78 C 78 48, 80.5 54, 85 59.5 H 60 C 64.5 54, 67 48, 67 40.3 Z",
  "M56 59.5 H 89 V 65.7 H 56 Z",
]
WORD = "LUCERNA"

def rows(y0, y1, x0, x1, size, step, cls):
    out, y, i = [], y0, 0
    line = (" ".join([WORD] * 12))
    while y <= y1:
        x = x0 - (i % 2) * size * 2.2
        out.append(f'<text class="{cls}" x="{x:.1f}" y="{y:.1f}" font-size="{size}">{line}</text>')
        y += step; i += 1
    return "\n    ".join(out)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" role="img" aria-label="Lucerna">
  <defs>
    <linearGradient id="gold" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#F7C75A"/><stop offset="1" stop-color="#B8730E"/>
    </linearGradient>
    <linearGradient id="smoke" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#5B47C4"/><stop offset="1" stop-color="#9A88EC"/>
    </linearGradient>
    <clipPath id="lamp">{"".join(f'<path d="{d}"/>' for d in LAMP)}<circle cx="74.5" cy="75.2" r="3.4"/></clipPath>
    <clipPath id="bishop">{"".join(f'<path d="{d}"/>' for d in BISHOP)}<circle cx="72.5" cy="8" r="3.8"/></clipPath>
    <path id="wisp" d="M20 79 C 12 70, 18 62, 28 63 C 38 64, 46 60, 58 61"/>
  </defs>
  <style>
    text {{ font-family: Georgia, 'Times New Roman', serif; font-weight: 700; letter-spacing: 0.25px; }}
    .g {{ fill: url(#gold); }}
    .p {{ fill: url(#smoke); }}
  </style>

  <g fill="#F2B544" fill-opacity="0.14" stroke="#D99A2B" stroke-opacity="0.55" stroke-width="0.7">
    {"".join(f'<path d="{d}"/>' for d in LAMP)}<circle cx="74.5" cy="75.2" r="3.4"/>
  </g>
  <g clip-path="url(#lamp)">
    {rows(73.2, 118, 16, 120, 3.0, 3.05, "g")}
  </g>
  <g fill="#8E7BE3" fill-opacity="0.14" stroke="#7C6AD6" stroke-opacity="0.55" stroke-width="0.7">
    {"".join(f'<path d="{d}"/>' for d in BISHOP)}<circle cx="72.5" cy="8" r="3.8"/>
  </g>
  <circle cx="72.5" cy="8" r="2.2" fill="url(#smoke)"/>
  <g clip-path="url(#bishop)">
    {rows(6.5, 66, 54, 92, 2.6, 2.65, "p")}
  </g>
  <text font-size="7.2" fill="#8E7BE3" font-style="italic" letter-spacing="0.6">
    <textPath href="#wisp" startOffset="4%">Lucerna</textPath>
  </text>
  <g fill="#E9B13F">
    <path d="M100 22 l1.6 4.4 4.4 1.6 -4.4 1.6 -1.6 4.4 -1.6 -4.4 -4.4 -1.6 4.4 -1.6 z"/>
    <path d="M44 36 l1.1 3 3 1.1 -3 1.1 -1.1 3 -1.1 -3 -3 -1.1 3 -1.1 z"/>
  </g>
</svg>
'''
open("docs/logo-calligram.svg", "w", encoding="utf-8").write(svg)
open("docs/_compare.html", "w", encoding="utf-8").write('''<!doctype html><meta charset="utf-8"><title>calligram</title>
<body style="margin:0;padding:20px;background:#FBF8F3;display:flex;gap:30px;align-items:center">
<img src="logo-calligram.svg?c=1" width="560"><img src="logo-calligram.svg?c=1" width="160"><img src="logo.svg" width="160">
</body>''')
print("ok")
