// basisu_text_image.h
// Copyright (C) 2019-2025 Binomial LLC. All Rights Reserved.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Text<->image conversion helpers for basisu_tool: compose a small PNG from a comma-separated
// pixel text file (and the inverse, dumping a PNG back to that text format). Intended for authoring
// tiny test images by hand (or with an AI) and for round-tripping them.
//
// Text format:
//   Line 1 (header):  W,H,C    where W,H are in [1,1024] and C (channels) is in [1,4].
//   Following lines:  one pixel per line, C comma-separated decimal values in [0,255], in
//                     row-major order (left->right, top->bottom). Exactly W*H pixels are required.
//   Blank lines and lines beginning with '#' (after leading whitespace) are ignored.
//
//   Channel meaning: 1 = L (gray, A=255), 2 = LA, 3 = RGB (A=255), 4 = RGBA.
#pragma once

namespace basisu
{
	// Reads the pixel text file pInput_txt_filename and writes pOutput_png_filename.
	// Parsing is strict: any malformed value, wrong per-line value count, bad header, or a pixel
	// count that does not equal W*H is a hard error (reported with the offending file line number).
	// Returns true on success.
	bool text_to_png(const char* pInput_txt_filename, const char* pOutput_png_filename);

	// Loads pInput_png_filename, auto-detects the minimal channel count needed to represent it
	// exactly (1=L, 2=LA, 3=RGB, 4=RGBA), and writes it as the pixel text format above to
	// pOutput_txt_filename. The image dimensions must be in [1,1024]. Returns true on success.
	bool png_to_text(const char* pInput_png_filename, const char* pOutput_txt_filename);

} // namespace basisu
