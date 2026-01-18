#!/usr/bin/python3

import os
from PIL import Image

def convert_bmp_to_clk(input_folder, output_folder="clk"):
    # Create output folder if missing
    os.makedirs(output_folder, exist_ok=True)

    # List BMP files
    bmp_files = [f for f in os.listdir(input_folder) if f.lower().endswith(".bmp")]
    if not bmp_files:
        print("No .bmp files found.")
        return

    for filename in bmp_files:
        bmp_path = os.path.join(input_folder, filename)
        base_name = os.path.splitext(filename)[0]
        clk_path = os.path.join(output_folder, base_name + ".clk")

        print(f"Processing: {filename}")

        # Load image
        img = Image.open(bmp_path).convert("RGB")
        W, H = img.size
        pixels = img.load()

        # Open output file
        with open(clk_path, "wb") as fout:
            # Write header (6 bytes)
            fout.write(b"C")
            fout.write(b"K")
            fout.write(W.to_bytes(2, "little"))
            fout.write(H.to_bytes(2, "little"))

            # Write pixel data in RGB565
            for y in range(H):
                for x in range(W):
                    R, G, B = pixels[x, y]

                    # Convert RGB888 → RGB565
                    out_pixel = (
                        ((R & 0xF8) << 8) |
                        ((G & 0xFC) << 3) |
                        (B >> 3)
                    )

                    fout.write(out_pixel.to_bytes(2, "little"))

        print(f"Saved: {clk_path}")

    print("\nDone!")


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Convert BMP files to .clk format")
    parser.add_argument("folder", help="Input folder containing .bmp images")
    parser.add_argument("--out", default="clk", help="Output folder for .clk files")

    args = parser.parse_args()

    convert_bmp_to_clk(args.folder, args.out)

