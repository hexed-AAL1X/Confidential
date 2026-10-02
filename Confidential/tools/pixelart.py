#!/usr/bin/env python3
"""Convierte fotos en fondos estilo pixel art (baja resolución + paleta reducida).

Uso: python3 tools/pixelart.py entrada.png salida.png [ancho] [colores]
"""
import sys

from PIL import Image, ImageEnhance, ImageFilter


def pixelart(src, dst, width=224, colors=24):
    img = Image.open(src).convert("RGB")
    img = ImageEnhance.Contrast(img).enhance(1.2)
    img = ImageEnhance.Color(img).enhance(1.35)

    height = max(1, round(img.height * width / img.width))
    small = img.resize((width, height), Image.BOX)

    quant = small.quantize(
        colors=colors,
        method=Image.Quantize.MEDIANCUT,
        kmeans=4,
        dither=Image.Dither.NONE,
    )
    cleaned = quant.convert("RGB").filter(ImageFilter.ModeFilter(3))
    final = cleaned.quantize(palette=quant, dither=Image.Dither.NONE)
    final.convert("RGB").save(dst)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 224
    c = int(sys.argv[4]) if len(sys.argv) > 4 else 24
    pixelart(sys.argv[1], sys.argv[2], w, c)
