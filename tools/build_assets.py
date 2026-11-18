"""Package the approved icon into Windows ICO and a matching SVG wordmark.

Requires Pillow only when regenerating assets, not when building the app.
"""
from pathlib import Path
import base64

from PIL import Image

brand = Path(__file__).resolve().parents[1] / 'resources' / 'brand'
source = brand / 'icon.png'
icon = Image.open(source).convert('RGBA')
icon.resize((256, 256), Image.Resampling.LANCZOS).save(
    brand / 'app.ico', format='ICO',
    sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)],
)
encoded = base64.b64encode(source.read_bytes()).decode('ascii')
svg = '''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="128" viewBox="0 0 512 128" role="img" aria-label="轻透截图">
  <style>.name{fill:#222}@media(prefers-color-scheme:dark){.name{fill:#f2f2f2}}</style>
  <image x="0" y="0" width="128" height="128" href="data:image/png;base64,ICON_DATA"/>
  <text class="name" x="152" y="88" font-family="Segoe UI,Microsoft YaHei UI,sans-serif" font-size="72" font-weight="600">轻透截图</text>
</svg>
'''
(brand / 'logo.svg').write_text(svg.replace('ICON_DATA', encoded), encoding='utf-8')
print('Updated resources/brand/app.ico and logo.svg')
