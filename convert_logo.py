from PIL import Image
import sys

def convert_to_bitmap(img_path, width, height):
    img = Image.open(img_path).convert('RGBA')
    # Flatten on white bg
    background = Image.new('RGBA', img.size, (255, 255, 255))
    img = Image.alpha_composite(background, img).convert('L')
    
    # Calculate aspect ratio preserving resize
    w, h = img.size
    ratio = min(width/w, height/h)
    new_w = int(w * ratio)
    new_h = int(h * ratio)
    img = img.resize((new_w, new_h), Image.Resampling.LANCZOS)
    
    # Binarize (1 for logo, 0 for bg)
    threshold = 128
    img = img.point(lambda p: 1 if p < threshold else 0, mode='1')
    
    # Center on target frame
    final_img = Image.new('1', (width, height), 0)
    offset_x = (width - new_w) // 2
    offset_y = (height - new_h) // 2
    final_img.paste(img, (offset_x, offset_y))
    
    # Convert to bytes
    hex_data = []
    for y in range(height):
        # width pixels packed into bytes
        for x_byte in range((width + 7) // 8):
            byte = 0
            for bit in range(8):
                x = x_byte * 8 + bit
                if x < width:
                    if final_img.getpixel((x, y)):
                        byte |= (1 << (7 - bit))
            hex_data.append(f"0x{byte:02X}")
    return hex_data




if __name__ == "__main__":
    images = [
        ("saw", "C:/Users/sjfau/.gemini/antigravity/brain/9c1d3d05-b7f5-464c-903b-1bad02d1d9a3/uploaded_media_0_1769623912215.png"),
        ("posipro", "C:/Users/sjfau/.gemini/antigravity/brain/9c1d3d05-b7f5-464c-903b-1bad02d1d9a3/uploaded_media_1_1769623912215.png")
    ]
    
    print("#ifndef LOGOS_H")
    print("#define LOGOS_H\n")
    print("#include <Arduino.h>\n")
    
    for name, path in images:
        hex_data = convert_to_bitmap(path, 72, 40)
        print(f"// Logo {name}: 72x40")
        print(f"static const uint8_t PROGMEM logo_{name}_bmp[] = {{")
        for i in range(0, len(hex_data), 9):
            print("    " + ", ".join(hex_data[i:i+9]) + ",")
        print("};\n")
        
    print("#endif // LOGOS_H")


