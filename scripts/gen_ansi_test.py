# Generating a clean ANSI file without BOM
content = """PB 1
BEGIN
[1
    A I 0.0
    = Q 0.0
BE

BLOCK PB 2
BEGIN
[1
    A I 0.0
    = Q 0.0
BE

PROGRAM BLOCK 3
BEGIN
[1
    A I 0.0
    = Q 0.0
BE
"""

with open("c:/data/BISSO_E350_Controller/bisso_s5_temp/IBH_STRICT_ANSI_TEST.AWL", "wb") as f:
    # Write as Windows-1252 (ANSI)
    f.write(content.encode('cp1252'))

print("Generated IBH_STRICT_ANSI_TEST.AWL in ANSI encoding")
