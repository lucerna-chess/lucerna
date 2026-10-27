"""Builds docs/logo-horizontal.svg and docs/logo-stacked.svg from docs/logo.svg.

Run from the repository root: python tools/make_wordmarks.py
"""
src = open("docs/logo.svg", encoding="utf-8").read()
inner = src[src.index(">", src.index("<svg")) + 1 : src.rindex("</svg>")]

style = """
  <style>
    .word { fill: #1D1B18; font-family: 'Source Serif 4', 'Iowan Old Style', 'Palatino Linotype', Georgia, serif; font-weight: 600; }
    .tag { fill: #6E675E; font-family: 'Segoe UI', system-ui, sans-serif; font-weight: 600; }
    @media (prefers-color-scheme: dark) { .word { fill: #F6F1E7; } .tag { fill: #B9AE9E; } }
  </style>"""

horizontal = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 470 128" role="img" aria-label="Lucerna">{style}
  <g>{inner}</g>
  <text class="word" x="140" y="78" font-size="62">Lucerna</text>
  <text class="tag" x="143" y="102" font-size="12.5" letter-spacing="4">CHESS ENGINE</text>
</svg>
'''

stacked = f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 260 240" role="img" aria-label="Lucerna">{style}
  <g transform="translate(66 0)">{inner}</g>
  <text class="word" x="132" y="184" text-anchor="middle" font-size="44">Lucerna</text>
  <text class="tag" x="132" y="208" text-anchor="middle" font-size="10" letter-spacing="5">CHESS ENGINE</text>
</svg>
'''

open("docs/logo-horizontal.svg", "w", encoding="utf-8").write(horizontal)
open("docs/logo-stacked.svg", "w", encoding="utf-8").write(stacked)
print("wrote docs/logo-horizontal.svg and docs/logo-stacked.svg")
