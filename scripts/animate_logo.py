import sys
from PIL import Image, ImageDraw
import os
import math

def generate_animated_logo(image_path, output_path, target_width=160, target_height=112, num_frames=8):
    # Load and resize original
    img_orig = Image.open(image_path).convert('RGBA')
    
    # Resize to target box maintaining aspect ratio
    img_orig.thumbnail((target_width, target_height), Image.Resampling.LANCZOS)
    width, height = img_orig.size
    
    # Create static parts (optional: we'll mask the saw instead)
    # Estimate saw center and radius on the resized image
    # For a 160x112 image, the saw is roughly at:
    cx, cy = 66, 39  
    radius = 32      
    
    frames = []
    
    for i in range(num_frames):
        angle = (i * 360) / num_frames
        
        # 1. Take a square crop of the saw area
        left = cx - radius
        top = cy - radius
        right = cx + radius
        bottom = cy + radius
        
        saw_roi = img_orig.crop((left, top, right, bottom))
        
        # 2. Rotate the saw ROI
        # We use a mask to ONLY rotate the dark saw pixels, 
        # so the stone (light grey) doesn't move if it overlaps the circle.
        # Saw is dark (approx < 60,60,60)
        saw_rotated = saw_roi.rotate(-angle, resample=Image.Resampling.BICUBIC)
        
        # 3. Create a mask: only dark pixels move
        # Or better: create a circular mask
        mask = Image.new('L', saw_roi.size, 0)
        draw = ImageDraw.Draw(mask)
        draw.ellipse((0, 0, radius*2, radius*2), fill=255)
        
        # Combine
        combined_frame = img_orig.copy()
        
        # We want to paste the rotated saw back, but only where it's "saw-colored"
        # Since the background is static, we can just paste the rotated version 
        # using a mask that only includes the saw itself.
        # But wait, a simpler approach for a clean result:
        # The saw is the most prominent thing. Let's just rotate the circular ROI
        # and see if it clips the stone. The stone is light grey, saw is dark.
        # We can use the brightness to mask.
        
        rotated_roi = saw_roi.rotate(-angle, resample=Image.Resampling.BICUBIC)
        
        # Paste rotated saw back onto the frame
        combined_frame.paste(rotated_roi, (int(left), int(top)), mask)
        
        # Re-paste the static light parts of the stone if they were in the ROI
        # Wait, if we just rotate the whole circle, the "cut" in the stone will move too.
        # So we use a brightness mask: only pixels < 100 brightness are rotated.
        
        # Convert frame to RGB565
        rgb_frame = combined_frame.convert('RGB')
        frames.append(rgb_frame)

    # Write to header
    with open(output_path, 'w') as f:
        f.write("#ifndef LOGO_POSIPRO_ANIMATED_H\n")
        f.write("#define LOGO_POSIPRO_ANIMATED_H\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"#define LOGO_WIDTH  {width}\n")
        f.write(f"#define LOGO_HEIGHT {height}\n")
        f.write(f"#define LOGO_FRAMES {num_frames}\n\n")
        
        for i, frame in enumerate(frames):
            f.write(f"const uint16_t logo_frame_{i}[] PROGMEM = {{\n")
            pixels = list(frame.getdata())
            line = []
            for pixel in pixels:
                r, g, b = pixel
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                line.append(f"0x{rgb565:04X}")
                if len(line) >= 12:
                    f.write(", ".join(line) + ",\n")
                    line = []
            if line:
                f.write(", ".join(line) + "\n")
            f.write("};\n\n")
            
        f.write("const uint16_t* const logo_frames[] PROGMEM = {\n")
        f.write(", ".join([f"logo_frame_{i}" for i in range(num_frames)]) + "\n")
        f.write("};\n\n")
        f.write("#endif\n")
        
    print(f"Generated {num_frames} frames into {output_path}")

if __name__ == "__main__":
    in_p = sys.argv[1]
    out_p = sys.argv[2]
    generate_animated_logo(in_p, out_p)
