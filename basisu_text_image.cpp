// basisu_text_image.cpp
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
// See basisu_text_image.h for the text format description.
#include "basisu_text_image.h"
#include "encoder/basisu_enc.h"

#include <string>
#include <vector>

namespace basisu
{
	// Max image width/height accepted/emitted by the text format (these are intended for small test images).
	const uint32_t TEXT_IMAGE_MAX_DIM = 1024;

	// Trim leading/trailing ASCII whitespace. Includes '\r', so the trailing CR of a CRLF line
	// is stripped after the caller has split the buffer on '\n'.
	static std::string trim_ws(const std::string& s)
	{
		size_t b = 0, e = s.size();
		const auto is_ws = [](char c) { return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n') || (c == '\f') || (c == '\v'); };
		while ((b < e) && is_ws(s[b])) b++;
		while ((e > b) && is_ws(s[e - 1])) e--;
		return s.substr(b, e - b);
	}

	// Parses a non-empty run of decimal digits with value in [0, max_val]. Rejects empty strings,
	// signs, and any non-digit character. Used for both header fields and pixel values.
	static bool parse_decimal_uint(const std::string& tok, uint32_t max_val, uint32_t& out_val)
	{
		if (tok.empty())
			return false;
		uint64_t v = 0;
		for (size_t i = 0; i < tok.size(); i++)
		{
			const char c = tok[i];
			if ((c < '0') || (c > '9'))
				return false;
			v = v * 10 + (uint64_t)(c - '0');
			if (v > max_val)
				return false; // out of range (also guards against overflow)
		}
		out_val = (uint32_t)v;
		return true;
	}

	// Splits a line on commas into trimmed tokens.
	static void split_commas(const std::string& s, std::vector<std::string>& out_tokens)
	{
		out_tokens.resize(0);
		size_t start = 0;
		for (;;)
		{
			const size_t comma = s.find(',', start);
			if (comma == std::string::npos)
			{
				out_tokens.push_back(trim_ws(s.substr(start)));
				break;
			}
			out_tokens.push_back(trim_ws(s.substr(start, comma - start)));
			start = comma + 1;
		}
	}

	bool text_to_png(const char* pInput_txt_filename, const char* pOutput_png_filename)
	{
		uint8_vec file_data;
		if (!read_file_to_vec(pInput_txt_filename, file_data))
		{
			error_printf("text_to_png: failed reading input text file \"%s\"\n", pInput_txt_filename);
			return false;
		}

		const char* pData = file_data.size() ? (const char*)&file_data[0] : "";
		const size_t data_size = file_data.size();

		uint32_t width = 0, height = 0, channels = 0;
		uint64_t total_pixels = 0, pixels_written = 0;
		bool have_header = false;
		image img;
		std::vector<std::string> tokens;

		size_t pos = 0;
		uint32_t line_no = 0;
		while (pos < data_size)
		{
			// Carve out one physical line [pos, line_end).
			size_t line_end = pos;
			while ((line_end < data_size) && (pData[line_end] != '\n'))
				line_end++;
			line_no++;

			const std::string line = trim_ws(std::string(pData + pos, pData + line_end));
			pos = line_end + 1;

			// Skip blank lines and '#' comments.
			if (line.empty() || (line[0] == '#'))
				continue;

			split_commas(line, tokens);

			if (!have_header)
			{
				if (tokens.size() != 3)
				{
					error_printf("text_to_png: line %u: header must be \"W,H,C\" (got %u value(s))\n", line_no, (uint32_t)tokens.size());
					return false;
				}
				if (!parse_decimal_uint(tokens[0], TEXT_IMAGE_MAX_DIM, width) || (width < 1) ||
					!parse_decimal_uint(tokens[1], TEXT_IMAGE_MAX_DIM, height) || (height < 1) ||
					!parse_decimal_uint(tokens[2], 4, channels) || (channels < 1))
				{
					error_printf("text_to_png: line %u: invalid header \"%s\"; expected W,H,C with W,H in [1,1024] and C in [1,4]\n", line_no, line.c_str());
					return false;
				}

				total_pixels = (uint64_t)width * height;
				img.resize(width, height);
				have_header = true;
				continue;
			}

			// Pixel line: exactly 'channels' decimal values in [0,255].
			if (tokens.size() != channels)
			{
				error_printf("text_to_png: line %u: expected %u value(s) per pixel, got %u: \"%s\"\n", line_no, channels, (uint32_t)tokens.size(), line.c_str());
				return false;
			}

			uint32_t v[4] = { 0, 0, 0, 0 };
			for (uint32_t c = 0; c < channels; c++)
			{
				if (!parse_decimal_uint(tokens[c], 255, v[c]))
				{
					error_printf("text_to_png: line %u: \"%s\" is not a decimal value in [0,255]\n", line_no, tokens[c].c_str());
					return false;
				}
			}

			if (pixels_written >= total_pixels)
			{
				error_printf("text_to_png: line %u: too many pixels (expected %llu for %ux%u)\n", line_no, (unsigned long long)total_pixels, width, height);
				return false;
			}

			const uint32_t x = (uint32_t)(pixels_written % width), y = (uint32_t)(pixels_written / width);
			color_rgba& d = img(x, y);
			switch (channels)
			{
			case 1: d.set((uint8_t)v[0], (uint8_t)v[0], (uint8_t)v[0], 255); break;			// L
			case 2: d.set((uint8_t)v[0], (uint8_t)v[0], (uint8_t)v[0], (uint8_t)v[1]); break;	// LA
			case 3: d.set((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], 255); break;			// RGB
			default: d.set((uint8_t)v[0], (uint8_t)v[1], (uint8_t)v[2], (uint8_t)v[3]); break;	// RGBA
			}
			pixels_written++;
		}

		if (!have_header)
		{
			error_printf("text_to_png: \"%s\" has no header line (expected W,H,C on the first non-blank, non-comment line)\n", pInput_txt_filename);
			return false;
		}

		if (pixels_written != total_pixels)
		{
			error_printf("text_to_png: expected %llu pixel(s) for %ux%u, got %llu\n", (unsigned long long)total_pixels, width, height, (unsigned long long)pixels_written);
			return false;
		}

		if (!save_png(pOutput_png_filename, img))
		{
			error_printf("text_to_png: failed writing output PNG \"%s\"\n", pOutput_png_filename);
			return false;
		}

		printf("Wrote %ux%u %u-channel image to \"%s\" (%llu pixels)\n", width, height, channels, pOutput_png_filename, (unsigned long long)total_pixels);
		return true;
	}

	bool png_to_text(const char* pInput_png_filename, const char* pOutput_txt_filename)
	{
		image img;
		if (!load_image(pInput_png_filename, img))
		{
			error_printf("png_to_text: failed loading image \"%s\"\n", pInput_png_filename);
			return false;
		}

		const uint32_t width = img.get_width(), height = img.get_height();
		if ((width < 1) || (width > TEXT_IMAGE_MAX_DIM) || (height < 1) || (height > TEXT_IMAGE_MAX_DIM))
		{
			error_printf("png_to_text: image is %ux%u; dimensions must be in [1,%u]\n", width, height, TEXT_IMAGE_MAX_DIM);
			return false;
		}

		// Auto-detect the minimal channel count: color if any pixel has R!=G or G!=B, alpha if any pixel has A!=255.
		bool is_color = false, has_alpha = false;
		for (uint32_t y = 0; y < height; y++)
		{
			for (uint32_t x = 0; x < width; x++)
			{
				const color_rgba& c = img(x, y);
				if ((c.r != c.g) || (c.g != c.b))
					is_color = true;
				if (c.a != 255)
					has_alpha = true;
			}
		}
		const uint32_t channels = is_color ? (has_alpha ? 4u : 3u) : (has_alpha ? 2u : 1u);
		const char* pChannels_desc = (channels == 1) ? "L" : (channels == 2) ? "LA" : (channels == 3) ? "RGB" : "RGBA";

		std::string out;
		out.reserve((size_t)width * height * 12 + 64);

		char buf[256];
		snprintf(buf, sizeof(buf), "# %ux%u, %u channel(s) (%s)\n", width, height, channels, pChannels_desc);
		out += buf;
		snprintf(buf, sizeof(buf), "%u,%u,%u\n", width, height, channels);
		out += buf;

		for (uint32_t y = 0; y < height; y++)
		{
			for (uint32_t x = 0; x < width; x++)
			{
				const color_rgba& c = img(x, y);
				switch (channels)
				{
				case 1: snprintf(buf, sizeof(buf), "%u\n", (uint32_t)c.r); break;									// L (== R since grayscale)
				case 2: snprintf(buf, sizeof(buf), "%u,%u\n", (uint32_t)c.r, (uint32_t)c.a); break;				// LA
				case 3: snprintf(buf, sizeof(buf), "%u,%u,%u\n", (uint32_t)c.r, (uint32_t)c.g, (uint32_t)c.b); break;	// RGB
				default: snprintf(buf, sizeof(buf), "%u,%u,%u,%u\n", (uint32_t)c.r, (uint32_t)c.g, (uint32_t)c.b, (uint32_t)c.a); break; // RGBA
				}
				out += buf;
			}
		}

		if (!write_data_to_file(pOutput_txt_filename, out.data(), out.size()))
		{
			error_printf("png_to_text: failed writing output text file \"%s\"\n", pOutput_txt_filename);
			return false;
		}

		printf("Wrote %ux%u %u-channel (%s) image as text to \"%s\" (%llu pixels)\n",
			width, height, channels, pChannels_desc, pOutput_txt_filename, (unsigned long long)((uint64_t)width * height));
		return true;
	}

} // namespace basisu
