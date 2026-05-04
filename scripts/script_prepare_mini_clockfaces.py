import os
import re
import shutil
import struct

Import("env")

TARGET_WIDTH = 80
TARGET_HEIGHT = 160

# Original clock display size used by most existing clockface packs.
# We first center each image on this virtual canvas (same idea as runtime DrawImage),
# then transform the whole canvas to TARGET size.
SOURCE_CANVAS_WIDTH = 135
SOURCE_CANVAS_HEIGHT = 240

# Resize mode for downscaling the virtual canvas to the Mini display size.
#   "contain" - fit canvas into target keeping aspect ratio, black bars may appear
#               (same as letterbox; no pixel is ever cropped)
#   "crop"    - fill target completely, cropping equally from both sides of the
#               longer axis (same as cover/zoom; no black bars, but edges are cut)
RESIZE_MODE = "contain"  # change to "crop" if you prefer no black bars

_CLOCK_IMAGE_RE = re.compile(r"^\d+\.(bmp|clk)$", re.IGNORECASE)


def _contain_resize_nearest(src_pixels, src_w, src_h, dst_w, dst_h, bg_value):
	"""Fit src into dst, preserving aspect ratio. Black bars fill unused area."""
	if src_w <= 0 or src_h <= 0:
		return [bg_value] * (dst_w * dst_h)

	scale = min(dst_w / float(src_w), dst_h / float(src_h))
	new_w = max(1, int(round(src_w * scale)))
	new_h = max(1, int(round(src_h * scale)))
	off_x = (dst_w - new_w) // 2
	off_y = (dst_h - new_h) // 2

	out = [bg_value] * (dst_w * dst_h)
	for y in range(new_h):
		src_y = min(src_h - 1, int(y / scale))
		for x in range(new_w):
			src_x = min(src_w - 1, int(x / scale))
			out[(off_y + y) * dst_w + (off_x + x)] = src_pixels[src_y * src_w + src_x]
	return out


def _crop_resize_nearest(src_pixels, src_w, src_h, dst_w, dst_h):
	"""Fill dst completely from src, cropping equally from both sides of the longer axis."""
	if src_w <= 0 or src_h <= 0:
		return [0] * (dst_w * dst_h)

	scale = max(dst_w / float(src_w), dst_h / float(src_h))
	scaled_w = max(1, int(round(src_w * scale)))
	scaled_h = max(1, int(round(src_h * scale)))
	crop_x = (scaled_w - dst_w) // 2
	crop_y = (scaled_h - dst_h) // 2

	out = [0] * (dst_w * dst_h)
	for y in range(dst_h):
		src_y = min(src_h - 1, int((y + crop_y) / scale))
		for x in range(dst_w):
			src_x = min(src_w - 1, int((x + crop_x) / scale))
			out[y * dst_w + x] = src_pixels[src_y * src_w + src_x]
	return out


def _center_on_canvas(src_pixels, src_w, src_h, canvas_w, canvas_h, bg_value):
	out = [bg_value] * (canvas_w * canvas_h)

	# Keep the same integer centering behavior as DrawImage in TFTs.cpp:
	# x = (TFT_WIDTH - w) / 2, y = (TFT_HEIGHT - h) / 2
	off_x = (canvas_w - src_w) // 2
	off_y = (canvas_h - src_h) // 2

	for y in range(src_h):
		dy = off_y + y
		if dy < 0 or dy >= canvas_h:
			continue
		for x in range(src_w):
			dx = off_x + x
			if dx < 0 or dx >= canvas_w:
				continue
			out[dy * canvas_w + dx] = src_pixels[y * src_w + x]

	return out


def _resize_from_virtual_canvas(src_pixels, src_w, src_h, bg_value):
	canvas_pixels = _center_on_canvas(
		src_pixels,
		src_w,
		src_h,
		SOURCE_CANVAS_WIDTH,
		SOURCE_CANVAS_HEIGHT,
		bg_value,
	)

	if RESIZE_MODE == "crop":
		return _crop_resize_nearest(
			canvas_pixels,
			SOURCE_CANVAS_WIDTH,
			SOURCE_CANVAS_HEIGHT,
			TARGET_WIDTH,
			TARGET_HEIGHT,
		)
	else:  # "contain" (default)
		return _contain_resize_nearest(
			canvas_pixels,
			SOURCE_CANVAS_WIDTH,
			SOURCE_CANVAS_HEIGHT,
			TARGET_WIDTH,
			TARGET_HEIGHT,
			bg_value,
		)


def _read_bmp(path):
	with open(path, "rb") as f:
		data = f.read()

	if len(data) < 54:
		raise ValueError("BMP too small")

	if data[0:2] != b"BM":
		raise ValueError("Not a BMP file")

	pixel_offset = struct.unpack_from("<I", data, 10)[0]
	dib_size = struct.unpack_from("<I", data, 14)[0]
	if dib_size < 40:
		raise ValueError("Unsupported BMP DIB header")

	width = struct.unpack_from("<i", data, 18)[0]
	height_signed = struct.unpack_from("<i", data, 22)[0]
	planes = struct.unpack_from("<H", data, 26)[0]
	bpp = struct.unpack_from("<H", data, 28)[0]
	compression = struct.unpack_from("<I", data, 30)[0]
	colors_used = struct.unpack_from("<I", data, 46)[0]

	if planes != 1:
		raise ValueError("Unsupported BMP planes")
	if compression != 0:
		raise ValueError("Compressed BMP is not supported")
	if bpp not in (1, 4, 8, 24):
		raise ValueError("Unsupported BMP bit depth")

	top_down = height_signed < 0
	width_abs = abs(width)
	height_abs = abs(height_signed)

	if width_abs == 0 or height_abs == 0:
		raise ValueError("Invalid BMP size")

	palette = []
	if bpp <= 8:
		palette_entries = colors_used if colors_used else (1 << bpp)
		pal_off = 14 + dib_size
		pal_len = palette_entries * 4
		if pal_off + pal_len > len(data):
			raise ValueError("Invalid BMP palette")
		for i in range(palette_entries):
			b, g, r, _ = struct.unpack_from("<BBBB", data, pal_off + i * 4)
			palette.append((r, g, b))

	row_bits = width_abs * bpp
	row_size = ((row_bits + 31) // 32) * 4
	needed = pixel_offset + row_size * height_abs
	if needed > len(data):
		raise ValueError("BMP pixel data truncated")

	pixels = [0] * (width_abs * height_abs)
	for src_row in range(height_abs):
		row_start = pixel_offset + src_row * row_size
		dst_y = src_row if top_down else (height_abs - 1 - src_row)

		if bpp == 24:
			for x in range(width_abs):
				b, g, r = struct.unpack_from("<BBB", data, row_start + x * 3)
				pixels[dst_y * width_abs + x] = (r, g, b)
		elif bpp == 8:
			for x in range(width_abs):
				pixels[dst_y * width_abs + x] = data[row_start + x]
		elif bpp == 4:
			for x in range(width_abs):
				packed = data[row_start + (x // 2)]
				pixels[dst_y * width_abs + x] = (packed >> 4) & 0x0F if (x % 2) == 0 else packed & 0x0F
		elif bpp == 1:
			for x in range(width_abs):
				packed = data[row_start + (x // 8)]
				bit = 7 - (x % 8)
				pixels[dst_y * width_abs + x] = (packed >> bit) & 0x01

	return {
		"width": width_abs,
		"height": height_abs,
		"bpp": bpp,
		"palette": palette,
		"pixels": pixels,
	}


def _write_bmp(path, width, height, bpp, palette, pixels):
	row_bits = width * bpp
	row_size = ((row_bits + 31) // 32) * 4
	pixel_data_size = row_size * height
	palette_data = b""

	if bpp <= 8:
		if not palette:
			raise ValueError("Missing palette for indexed BMP")
		palette_data = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in palette)

	file_header_size = 14
	dib_size = 40
	pixel_offset = file_header_size + dib_size + len(palette_data)
	file_size = pixel_offset + pixel_data_size

	out = bytearray()
	out += b"BM"
	out += struct.pack("<I", file_size)
	out += struct.pack("<I", 0)
	out += struct.pack("<I", pixel_offset)

	out += struct.pack("<I", dib_size)
	out += struct.pack("<i", width)
	out += struct.pack("<i", height)
	out += struct.pack("<H", 1)
	out += struct.pack("<H", bpp)
	out += struct.pack("<I", 0)
	out += struct.pack("<I", pixel_data_size)
	out += struct.pack("<i", 0)
	out += struct.pack("<i", 0)
	out += struct.pack("<I", len(palette) if bpp <= 8 else 0)
	out += struct.pack("<I", 0)

	out += palette_data

	for src_row in range(height - 1, -1, -1):
		row = pixels[src_row * width : (src_row + 1) * width]
		row_bytes = bytearray()

		if bpp == 24:
			for r, g, b in row:
				row_bytes += struct.pack("<BBB", b, g, r)
		elif bpp == 8:
			row_bytes += bytes(int(v) & 0xFF for v in row)
		elif bpp == 4:
			for i in range(0, len(row), 2):
				left = int(row[i]) & 0x0F
				right = int(row[i + 1]) & 0x0F if i + 1 < len(row) else 0
				row_bytes.append((left << 4) | right)
		elif bpp == 1:
			cur = 0
			bits = 0
			for v in row:
				cur = (cur << 1) | (int(v) & 0x01)
				bits += 1
				if bits == 8:
					row_bytes.append(cur)
					cur = 0
					bits = 0
			if bits:
				row_bytes.append(cur << (8 - bits))
		else:
			raise ValueError("Unsupported BMP bit depth")

		if len(row_bytes) < row_size:
			row_bytes += b"\x00" * (row_size - len(row_bytes))
		out += row_bytes

	with open(path, "wb") as f:
		f.write(out)


def _majority(values):
	"""Return the most common value from a list; ties broken by first occurrence."""
	counts = {}
	for v in values:
		counts[v] = counts.get(v, 0) + 1
	return max(values, key=lambda v: counts[v])


def _detect_bg_color(pixels, width, height):
	"""Sample the 4 corners and return the majority pixel value as background."""
	if width == 0 or height == 0:
		return pixels[0] if pixels else 0
	corners = [
		pixels[0],                                    # top-left
		pixels[width - 1],                            # top-right
		pixels[(height - 1) * width],                 # bottom-left
		pixels[(height - 1) * width + (width - 1)],   # bottom-right
	]
	return _majority(corners)


def _resize_bmp_in_place(path):
	bmp = _read_bmp(path)
	bg = _detect_bg_color(bmp["pixels"], bmp["width"], bmp["height"])
	resized_pixels = _resize_from_virtual_canvas(
		bmp["pixels"],
		bmp["width"],
		bmp["height"],
		bg,
	)
	_write_bmp(path, TARGET_WIDTH, TARGET_HEIGHT, bmp["bpp"], bmp["palette"], resized_pixels)


def _read_clk(path):
	with open(path, "rb") as f:
		data = f.read()

	if len(data) < 6 or data[0:2] != b"CK":
		raise ValueError("Not a CLK file")

	width = struct.unpack_from("<H", data, 2)[0]
	height = struct.unpack_from("<H", data, 4)[0]
	pixel_count = width * height
	expected_size = 6 + pixel_count * 2
	if len(data) < expected_size:
		raise ValueError("CLK pixel data truncated")

	pixels = [0] * pixel_count
	off = 6
	for i in range(pixel_count):
		pixels[i] = struct.unpack_from("<H", data, off + i * 2)[0]

	return width, height, pixels


def _write_clk(path, width, height, pixels):
	out = bytearray()
	out += b"CK"
	out += struct.pack("<H", width)
	out += struct.pack("<H", height)
	for p in pixels:
		out += struct.pack("<H", int(p) & 0xFFFF)
	with open(path, "wb") as f:
		f.write(out)


def _resize_clk_in_place(path):
	width, height, pixels = _read_clk(path)
	bg = _detect_bg_color(pixels, width, height)
	resized = _resize_from_virtual_canvas(pixels, width, height, bg)
	_write_clk(path, TARGET_WIDTH, TARGET_HEIGHT, resized)


def _prepare_mini_data_dir():
	project_dir = env.subst("$PROJECT_DIR")
	source_data_dir = os.path.join(project_dir, "data")
	generated_data_dir = os.path.join(project_dir, ".pio", "generated_data", "MarvelTubesMini")

	if not os.path.isdir(source_data_dir):
		print("[mini-data] ERROR: source data directory missing:", source_data_dir)
		env.Replace(PROJECT_DATA_DIR=source_data_dir)
		return

	if os.path.isdir(generated_data_dir):
		shutil.rmtree(generated_data_dir)

	shutil.copytree(source_data_dir, generated_data_dir)

	converted = 0
	skipped = 0
	for entry in sorted(os.listdir(generated_data_dir)):
		if not _CLOCK_IMAGE_RE.match(entry):
			continue

		file_path = os.path.join(generated_data_dir, entry)
		ext = os.path.splitext(entry)[1].lower()
		try:
			if ext == ".bmp":
				_resize_bmp_in_place(file_path)
				converted += 1
			elif ext == ".clk":
				_resize_clk_in_place(file_path)
				converted += 1
		except Exception as exc:
			skipped += 1
			print(f"[mini-data] WARNING: skipped {entry}: {exc}")

	print(
		f"[mini-data] Prepared {generated_data_dir} ({converted} converted, {skipped} skipped)."
	)
	env.Replace(PROJECT_DATA_DIR=generated_data_dir)


_prepare_mini_data_dir()
