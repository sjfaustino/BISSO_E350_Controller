import os
import re

def reformat_block(filepath, block_id):
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    # Extract block type and number from block_id (e.g., "PB20.AWL")
    match = re.match(r'([A-Z]+)(\d+)', block_id)
    if not match:
        return ""
    
    b_type = match.group(1)
    b_num = match.group(2)
    
    output = []
    output.append(f"{b_type} {b_num}\n")
    
    # Look for NAME: line (standard in FBs)
    name_line = ""
    for line in lines:
        if "NAME:" in line:
            name_line = line.strip()
            break
            
    if name_line:
        output.append(f"{name_line}\n")
    
    output.append("BEGIN\n")
    
    for line in lines:
        # Skip ###PG header, [n (segment start), and solitary ] or ***
        if line.startswith("###PG") or re.match(r'^\[\d+', line) or line.strip() == "]" or line.strip() == "***":
            continue
        
        # Clean line: remove markers like *** ] or BE ]
        clean_line = re.sub(r'(\*\*\*|BE|BE\t| \t)\s*\]', '', line).rstrip()
        
        # Skip the original NAME line since we moved it to header
        if "NAME:" in line.upper():
            continue
            
        # Avoid empty logic lines that were just segment markers
        if not clean_line:
            continue
            
        output.append(f"{clean_line}\n")
    
    # Ensure block ends with BE
    if not output or output[-1].strip() != "BE":
        output.append("BE\n")
    
    output.append("\n")
    return "".join(output)

def main():
    base_path = "c:/data/BISSO_E350_Controller/bisso_s5_temp/01 - STL"
    block_list_path = os.path.join(base_path, "Block list.awl")
    output_path = "c:/data/BISSO_E350_Controller/bisso_s5_temp/IBH_BISSO_PROJECT.AWL"
    
    with open(block_list_path, 'r') as f:
        blocks = [line.strip() for line in f if line.strip()]
        
    full_output = []
    for block in blocks:
        filepath = os.path.join(base_path, block)
        if os.path.exists(filepath):
            full_output.append(reformat_block(filepath, block))
            
    with open(output_path, 'w') as f:
        f.write("".join(full_output))
    
    print(f"Successfully generated {output_path}")

if __name__ == "__main__":
    main()
