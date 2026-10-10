"""Regenerates the native app icon (builds do not require Python/Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

size = 256
im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
draw = ImageDraw.Draw(im)
draw.rounded_rectangle((8, 8, 248, 248), radius=48, fill=(13, 15, 21, 255))
for row in range(8):
    for col in range(8):
        width = 117 + row * 10
        x = 128 + (col / 7 - 0.5) * width
        y = 71 + row * 16
        lift = max(0, 1 - abs(col - 5) / 2) * max(0, 1 - abs(row - 3) / 3) * 39
        r = 3 + row * 0.22
        draw.ellipse((x-r, y-lift-r, x+r, y-lift+r), fill=(255, int(lift*5), 40, 255))
im.save(Path(__file__).with_name("app.ico"), sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
