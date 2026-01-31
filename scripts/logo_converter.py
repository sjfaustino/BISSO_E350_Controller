import sys
from PIL import Image
import os

def convert_to_rgb565(image_path, output_path, target_width=None, target_height=None):
    img = Image.open(image_path).convert('RGB')
    
    if target_width and target_height:
        # Maintain aspect ratio within target box
        img.thumbnail((target_width, target_height), Image.Resampling.LANCZOS)
    
    width, height = img.size
    
    with open(output_path, 'w') as f:
        f.write("#ifndef LOGO_POSIPRO_RGB565_H\n")
        f.write("#define LOGO_POSIPRO_RGB565_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define LOGO_WIDTH  {width}\n")
        f.write(f"#define LOGO_HEIGHT {height}\n\n")
        f.write("const uint16_t logo_posipro_rgb565[] PROGMEM = {\n")
        
        count = 0
        for y in range(height):
            line = []
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                # Convert to RGB565 (5 bits R, 6 bits G, 5 bits B)
                # Big Endian (swapped later if needed by TFT_eSPI)
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                line.append(f"0x{rgb565:04X}")
                count += 1
            f.write(", ".join(line) + ",\n")
            
        f.write("};\n\n")
        f.write("#endif\n")
        
    print(f"Successfully converted {image_path} to {output_path}")
    print(f"Dimensions: {width}x{height} ({count} pixels)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python logo_converter.py <input_png> <output_h> [width] [height]")
    else:
        in_p = sys.argv[1]
        out_p = sys.argv[2]
        w = int(sys.argv[3]) if len(sys.argv) > 3 else 200
        h = int(sys.argv[4]) if len(sys.argv) > 4 else 115
        convert_to_rgb565(in_p, out_p, w, h)
