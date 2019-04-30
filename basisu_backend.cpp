// basisu_backend.cpp
// Copyright (C) 2017-2019 Binomial LLC. All Rights Reserved.
//
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
// TODO: This code originally supported full ETC1 and ETC1S, so there's some legacy stuff in here.
//
#include "basisu_backend.h"

#define DISABLE_CODEBOOK_REORDERING (0)
#define BASISU_BACKEND_VERIFY(c) verify(c, __LINE__);

namespace basisu
{
	const uint32_t TOTAL_MACROBLOCK_DIFF_BITS = 4;
	const uint32_t TOTAL_MACROBLOCK_FLIP_BITS = 4;

	// TODO
	static void verify(bool condition, int line)
	{
		if (!condition)
		{
			fprintf(stderr, "basisu_backend: verify() failed at line %i!\n", line);
			abort();
		}
	}

	basisu_backend::basisu_backend()
	{
		clear();
	}

	void basisu_backend::clear()
	{
		m_pFront_end = NULL;
		m_params.clear();
		m_output.clear();
	}

	void basisu_backend::init(basisu_frontend *pFront_end, basisu_backend_params &params, const basisu_backend_slice_desc_vec &slice_descs, const basist::etc1_global_selector_codebook *pGlobal_sel_codebook)
	{
		m_pFront_end = pFront_end;
		m_params = params;
		m_slices = slice_descs;
		m_pGlobal_sel_codebook = pGlobal_sel_codebook;

		debug_printf("basisu_backend::Init: Slices: %u, ETC1S: %u, DeltaSelectorRDOQualityThresh: %f, UseGlobalSelCodebook: %u, GlobalSelCodebookPalBits: %u, GlobalSelCodebookModBits: %u, Use hybrid selector codebooks: %u\n",
			m_slices.size(),
			params.m_etc1s,
			params.m_delta_selector_rdo_quality_thresh,
			params.m_use_global_sel_codebook,
			params.m_global_sel_codebook_pal_bits,
			params.m_global_sel_codebook_mod_bits,
			params.m_use_hybrid_sel_codebooks);

		for (uint32_t i = 0; i < m_slices.size(); i++)
		{
			debug_printf("Slice: %u, OrigWidth: %u, OrigHeight: %u, Width: %u, Height: %u, NumBlocksX: %u, NumBlocksY: %u, NumMacroBlocksX: %u, NumMacroBlocksY: %u, FirstBlockIndex: %u\n",
				i,
				m_slices[i].m_orig_width, m_slices[i].m_orig_height,
				m_slices[i].m_width, m_slices[i].m_height,
				m_slices[i].m_num_blocks_x, m_slices[i].m_num_blocks_y,
				m_slices[i].m_num_macroblocks_x, m_slices[i].m_num_macroblocks_y,
				m_slices[i].m_first_block_index);
		}
	}

	void basisu_backend::create_endpoint_palette()
	{
		const basisu_frontend &r = *m_pFront_end;

		m_endpoint_palette.resize(r.get_total_endpoint_clusters());
		for (uint32_t i = 0; i < r.get_total_endpoint_clusters(); i++)
		{
			etc1_endpoint_palette_entry &e = m_endpoint_palette[i];

			e.m_color5_valid = r.get_endpoint_cluster_color_is_used(i, false);
			if (e.m_color5_valid)
			{
				e.m_color5 = r.get_endpoint_cluster_unscaled_color(i, false);
				e.m_inten5 = r.get_endpoint_cluster_inten_table(i, false);
			}
			else
			{
				BASISU_BACKEND_VERIFY(false);
			}
		}
	}

	void basisu_backend::create_selector_palette()
	{
		const basisu_frontend &r = *m_pFront_end;

		m_selector_palette.resize(r.get_total_selector_clusters());

		if (m_params.m_use_global_sel_codebook)
		{
			m_global_selector_palette_desc.resize(r.get_total_selector_clusters());

			for (int i = 0; i < static_cast<int>(r.get_total_selector_clusters()); i++)
			{
				basist::etc1_selector_palette_entry &selector_pal_entry = m_selector_palette[i];

				etc1_global_selector_cb_entry_desc &pal_entry_desc = m_global_selector_palette_desc[i];
				pal_entry_desc.m_pal_index = r.get_selector_cluster_global_selector_entry_ids()[i].m_palette_index;
				pal_entry_desc.m_mod_index = r.get_selector_cluster_global_selector_entry_ids()[i].m_modifier.get_index();

				pal_entry_desc.m_was_used = true;
				if (m_params.m_use_hybrid_sel_codebooks)
					pal_entry_desc.m_was_used = r.get_selector_cluster_uses_global_cb_vec()[i];

				if (pal_entry_desc.m_was_used)
				{
					const etc_block &selector_bits = r.get_selector_cluster_selector_bits(i);
					(void)selector_bits;

					basist::etc1_selector_palette_entry global_pal_entry(m_pGlobal_sel_codebook->get_entry(r.get_selector_cluster_global_selector_entry_ids()[i]));

					for (uint32_t y = 0; y < 4; y++)
					{
						for (uint32_t x = 0; x < 4; x++)
						{
							selector_pal_entry(x, y) = global_pal_entry(x, y);

							assert(selector_bits.get_selector(x, y) == global_pal_entry(x, y));
						}
					}
				}
				else
				{
					const etc_block &selector_bits = r.get_selector_cluster_selector_bits(i);

					for (uint32_t y = 0; y < 4; y++)
						for (uint32_t x = 0; x < 4; x++)
							selector_pal_entry[y * 4 + x] = static_cast<uint8_t>(selector_bits.get_selector(x, y));
				}
			}
		}
		else
		{
			for (uint32_t i = 0; i < r.get_total_selector_clusters(); i++)
			{
				basist::etc1_selector_palette_entry &s = m_selector_palette[i];

				const etc_block &selector_bits = r.get_selector_cluster_selector_bits(i);

				for (uint32_t y = 0; y < 4; y++)
				{
					for (uint32_t x = 0; x < 4; x++)
					{
						s[y * 4 + x] = static_cast<uint8_t>(selector_bits.get_selector(x, y));
					}
				}
			}
		}
	}

	// endpoint palette
	//   5:5:5 and predicted 4:4:4 colors, 1 or 2 3-bit intensity table indices
	// selector palette
	//   4x4 2-bit selectors

	// per-macroblock:
	//  4 diff bits
	//  4 flip bits
	//  Endpoint template index, 1-8 endpoint indices
	//      Alternately, if no template applies, we can send 4 ETC1S bits followed by 4-8 endpoint indices
	//  4 selector indices

	float basisu_backend::selector_zeng_similarity_func(uint32_t index_a, uint32_t index_b, void *pContext)
	{
		basisu_backend& backend = *static_cast<basisu_backend*>(pContext);

		const basist::etc1_selector_palette_entry &a = backend.m_selector_palette[index_a];
		const basist::etc1_selector_palette_entry &b = backend.m_selector_palette[index_b];

		float total = static_cast<float>(a.calc_hamming_dist(b));

		float weight = 1.0f - clamp(total * (1.0f / 32.0f), 0.0f, 1.0f);
		return weight;
	}

	void basisu_backend::create_macroblocks()
	{
		const basisu_frontend &r = *m_pFront_end;

		m_slice_macroblocks.resize(m_slices.size());

		uint_vec all_endpoint_indices;
		uint_vec all_selector_indices;

		uint32_t total_template_exceptions = 0;

		for (uint32_t slice_index = 0; slice_index < m_slices.size(); slice_index++)
		{
			const uint32_t first_block_index = m_slices[slice_index].m_first_block_index;

			const uint32_t width = m_slices[slice_index].m_width;
			const uint32_t height = m_slices[slice_index].m_height;
			const uint32_t num_blocks_x = m_slices[slice_index].m_num_blocks_x;
			const uint32_t num_blocks_y = m_slices[slice_index].m_num_blocks_y;

			const uint32_t num_macroblocks_x = m_slices[slice_index].m_num_macroblocks_x;
			const uint32_t num_macroblocks_y = m_slices[slice_index].m_num_macroblocks_y;

			m_slice_macroblocks[slice_index].resize(num_macroblocks_x, num_macroblocks_y);

			for (uint32_t macroblock_y = 0; macroblock_y < num_macroblocks_y; macroblock_y++)
			{
				const uint32_t y = macroblock_y * 2;

				const int x_start = (macroblock_y & 1) ? (num_macroblocks_x - 1) : 0;
				const int x_end = (macroblock_y & 1) ? -1 : num_macroblocks_x;
				const int x_dir = (macroblock_y & 1) ? -1 : 1;

				for (int macroblock_x = x_start; macroblock_x != x_end; macroblock_x += x_dir)
				{
					const uint32_t x = macroblock_x * 2;

					uint32_t block_indices[4];
					block_indices[0] = first_block_index + x + y * num_blocks_x;
					block_indices[1] = first_block_index + minimum<int>(x + 1, num_blocks_x - 1) + y * num_blocks_x;
					block_indices[2] = first_block_index + x + minimum<int>(y + 1, num_blocks_y - 1) * num_blocks_x;
					block_indices[3] = first_block_index + minimum<int>(x + 1, num_blocks_x - 1) + minimum<int>(y + 1, num_blocks_y - 1) * num_blocks_x;

					etc_block macroblock[4];
					for (uint32_t i = 0; i < 4; i++)
						macroblock[i] = r.get_output_block(block_indices[i]);

					uint32_t flip_bits = 0;
					uint32_t diff_bits = 0;
					for (uint32_t k = 0; k < 4; k++)
					{
						flip_bits = (flip_bits << 1) | (macroblock[k].get_flip_bit() ? 1 : 0);
						diff_bits = (diff_bits << 1) | (macroblock[k].get_diff_bit() ? 1 : 0);
					}

					etc1_macroblock m;

					m.m_diff_bits = static_cast<uint8_t>(diff_bits);
					m.m_flip_bits = static_cast<uint8_t>(flip_bits);

					uint_vec endpoint_indices;

					for (uint32_t i = 0; i < 4; i++)
					{
						endpoint_indices.push_back(r.get_subblock_endpoint_cluster_index(block_indices[i], 0));
						endpoint_indices.push_back(r.get_subblock_endpoint_cluster_index(block_indices[i], 1));

						if (macroblock[i].get_diff_bit())
						{
							uint32_t e0 = r.get_subblock_endpoint_cluster_index(block_indices[i], 0);
							uint32_t e1 = r.get_subblock_endpoint_cluster_index(block_indices[i], 1);

							color_rgba c0(r.get_endpoint_cluster_unscaled_color(e0, false));
							color_rgba c1(r.get_endpoint_cluster_unscaled_color(e1, false));

							etc_block test_block;
							if (!test_block.set_block_color5_check(c0, c1))
							{
								BASISU_BACKEND_VERIFY(0);
							}
						}

						m.m_selector_indices.push_back(r.get_block_selector_cluster_index(block_indices[i]));
					}

					int_vec endpoint_palette;
					uint8_t endpoint_palette_indices[8];
					uint32_t n = 0;

					for (uint32_t ty = 0; ty < 2; ty++)
					{
						for (uint32_t tx = 0; tx < 2; tx++)
						{
							for (uint32_t t = 0; t < 2; t++)
							{
								int endpoint_index = r.get_subblock_endpoint_cluster_index(block_indices[tx + ty * 2], t);

								uint32_t p;
								for (p = 0; p < endpoint_palette.size(); p++)
									if (endpoint_palette[p] == endpoint_index)
										break;

								if (p >= endpoint_palette.size())
								{
									endpoint_palette.push_back(endpoint_index);
								}

								endpoint_palette_indices[n++] = static_cast<uint8_t>(p);
							}
						}
					}

					uint32_t t;
					for (t = 0; t < basist::TOTAL_ENDPOINT_INDEX_TEMPLATES; t++)
					{
						if (memcmp(endpoint_palette_indices, basist::g_endpoint_index_templates[t].m_local_indices, 8) == 0)
							break;
					}

					// TODO: There shouldn't be any exceptions in ETC1S
					if (t == basist::TOTAL_ENDPOINT_INDEX_TEMPLATES)
					{
						endpoint_palette.resize(0);
						n = 0;
						clear_obj(endpoint_palette_indices);

						for (uint32_t i = 0; i < 4; i++)
						{
							uint32_t endpoint_index0 = r.get_subblock_endpoint_cluster_index(block_indices[i], 0);
							uint32_t endpoint_index1 = r.get_subblock_endpoint_cluster_index(block_indices[i], 1);

							endpoint_palette_indices[n++] = static_cast<uint8_t>(endpoint_palette.size());
							endpoint_palette.push_back(endpoint_index0);

							if (endpoint_index0 != endpoint_index1)
							{
								endpoint_palette.push_back(endpoint_index1);
							}

							endpoint_palette_indices[n++] = static_cast<uint8_t>(endpoint_palette.size() - 1);
						}

						for (t = 0; t < basist::TOTAL_ENDPOINT_INDEX_TEMPLATES; t++)
						{
							if (memcmp(endpoint_palette_indices, basist::g_endpoint_index_templates[t].m_local_indices, 8) == 0)
								break;
						}

						BASISU_BACKEND_VERIFY(t != basist::TOTAL_ENDPOINT_INDEX_TEMPLATES);

						total_template_exceptions++;
					}

					m.m_template_index = t;
					m.m_endpoint_indices = endpoint_palette;

					for (uint32_t i = 0; i < 4; i++)
					{
						if (!macroblock[i].get_diff_bit())
							continue;

						uint32_t l0 = basist::g_endpoint_index_templates[t].m_local_indices[i * 2 + 0];
						uint32_t l1 = basist::g_endpoint_index_templates[t].m_local_indices[i * 2 + 1];

						uint32_t e0 = endpoint_palette[l0];
						uint32_t e1 = endpoint_palette[l1];

						//uint32_t e0 = r.get_subblock_endpoint_cluster_index(block_indices[i], 0);
						//uint32_t e1 = r.get_subblock_endpoint_cluster_index(block_indices[i], 1);

						color_rgba c0(r.get_endpoint_cluster_unscaled_color(e0, false));
						color_rgba c1(r.get_endpoint_cluster_unscaled_color(e1, false));

						etc_block test_block;
						if (!test_block.set_block_color5_check(c0, c1))
						{
							BASISU_BACKEND_VERIFY(0);
						}
					}

					m_slice_macroblocks[slice_index](macroblock_x, macroblock_y) = m;

					for (uint32_t i = 0; i < endpoint_palette.size(); i++)
						all_endpoint_indices.push_back(endpoint_palette[i]);

					for (uint32_t i = 0; i < m.m_selector_indices.size(); i++)
						all_selector_indices.push_back(m.m_selector_indices[i]);

				} // macroblock_x

			} // macroblock_y
		} // slice

		debug_printf("Total template exception: %u out of %u %3.1f%%\n", total_template_exceptions, get_total_macroblocks(), total_template_exceptions * 100.0f / get_total_macroblocks());

#if DISABLE_CODEBOOK_REORDERING
		m_endpoint_remap_table_old_to_new.resize(r.get_total_endpoint_clusters());
		for (uint32_t i = 0; i < r.get_total_endpoint_clusters(); i++)
			m_endpoint_remap_table_old_to_new[i] = i;

		m_selector_remap_table_old_to_new.resize(r.get_total_selector_clusters());
		for (uint32_t i = 0; i < r.get_total_selector_clusters(); i++)
			m_selector_remap_table_old_to_new[i] = i;
#else
		{
			//create_zeng_reorder_table(r.get_total_endpoint_clusters(), all_endpoint_indices.size(), all_endpoint_indices.get_ptr(), m_endpoint_remap_table_old_to_new, NULL, NULL, 0.0f);

			palette_index_reorderer reorderer;
			reorderer.init((uint32_t)all_endpoint_indices.size(), &all_endpoint_indices[0], r.get_total_endpoint_clusters(), nullptr, nullptr, 0);
			m_endpoint_remap_table_old_to_new = reorderer.get_remap_table();
		}

		// Maps old to new selector indices
		{
			//const float selector_similarity_func_weight = 1.0f;
			//create_zeng_reorder_table(r.get_total_selector_clusters(), all_selector_indices.size(), all_selector_indices.get_ptr(), m_selector_remap_table_old_to_new, selector_zeng_similarity_func, this, selector_similarity_func_weight);
			//create_zeng_reorder_table(r.get_total_selector_clusters(), all_selector_indices.size(), all_selector_indices.get_ptr(), m_selector_remap_table_old_to_new, NULL, NULL, 0.0f);

			palette_index_reorderer reorderer;
			reorderer.init((uint32_t)all_selector_indices.size(), &all_selector_indices[0], r.get_total_selector_clusters(), nullptr, nullptr, 0);
			m_selector_remap_table_old_to_new = reorderer.get_remap_table();
		}

#endif
		m_endpoint_remap_table_new_to_old.resize(r.get_total_endpoint_clusters());
		for (uint32_t i = 0; i < m_endpoint_remap_table_old_to_new.size(); i++)
			m_endpoint_remap_table_new_to_old[m_endpoint_remap_table_old_to_new[i]] = i;

		// Maps new to old selector indices
		m_selector_remap_table_new_to_old.resize(r.get_total_selector_clusters());
		for (uint32_t i = 0; i < m_selector_remap_table_old_to_new.size(); i++)
			m_selector_remap_table_new_to_old[m_selector_remap_table_old_to_new[i]] = i;

		if (!m_params.m_use_global_sel_codebook)
			optimize_selector_palette_order(all_selector_indices);
	}

	void basisu_backend::optimize_selector_palette_order(const uint_vec &all_selector_indices)
	{
		const basisu_frontend &r = *m_pFront_end;

		uint_vec new_selector_hist(r.get_total_selector_clusters());
		for (uint32_t i = 0; i < all_selector_indices.size(); i++)
			new_selector_hist[m_selector_remap_table_old_to_new[all_selector_indices[i]]]++;

		uint32_t max_hist_value = 0;
		uint32_t max_hist_value_index = 0;
		for (uint32_t i = 0; i < new_selector_hist.size(); i++)
		{
			if (new_selector_hist[i] > max_hist_value)
			{
				max_hist_value = new_selector_hist[i];
				max_hist_value_index = i;
			}
		}

		uint_vec optimized_selector_order;

		const uint32_t N = 32;
		for (uint32_t i = 0; i < r.get_total_selector_clusters(); i += N)
		{
			const uint32_t e = minimum<int>(i + N, r.get_total_selector_clusters());

			if (do_excl_ranges_overlap(i, e, static_cast<int>(max_hist_value_index) - 16, static_cast<int>(max_hist_value_index) + 16))
			{
				for (uint32_t j = i; j < e; j++)
					optimized_selector_order.push_back(j);
				continue;
			}

			basist::etc1_selector_palette_entry prev_entry(m_selector_palette[m_selector_remap_table_new_to_old[i]]);

			optimized_selector_order.push_back(i);

			uint_vec remaining_entries;
			for (uint32_t j = i + 1; j < e; j++)
				remaining_entries.push_back(j);

			for (uint32_t j = i + 1; j < e; j++)
			{
				uint32_t best_dist = UINT32_MAX;
				uint32_t best_entry = 0;

				for (uint32_t k = 0; k < remaining_entries.size(); k++)
				{
					uint32_t dist = prev_entry.calc_hamming_dist(m_selector_palette[m_selector_remap_table_new_to_old[remaining_entries[k]]]);
					if (dist < best_dist)
					{
						best_dist = dist;
						best_entry = k;
					}
				}

				optimized_selector_order.push_back(remaining_entries[best_entry]);

				prev_entry = m_selector_palette[m_selector_remap_table_new_to_old[remaining_entries[best_entry]]];

				remaining_entries.erase(remaining_entries.begin() + best_entry);
			}
		}

		uint_vec temp(r.get_total_selector_clusters());
		for (uint32_t i = 0; i < r.get_total_selector_clusters(); i++)
			temp[i] = m_selector_remap_table_new_to_old[optimized_selector_order[i]];

		m_selector_remap_table_new_to_old = temp;

		for (uint32_t i = 0; i < r.get_total_selector_clusters(); i++)
			m_selector_remap_table_old_to_new[m_selector_remap_table_new_to_old[i]] = i;
	}

	bool basisu_backend::encode_image()
	{
		const basisu_frontend &r = *m_pFront_end;

		uint_vec endpoint_histogram(r.get_total_endpoint_clusters() * 2);
		uint_vec selector_histogram(r.get_total_selector_clusters() * 2);
		uint_vec actual_selector_histogram(r.get_total_selector_clusters());
				
		// TODO: Choose the size in an intelligent way (try different sizes?)
		const uint32_t MAX_SELECTOR_HISTORY_BUF_SIZE = 64;
		basist::approx_move_to_front selector_history_buf(MAX_SELECTOR_HISTORY_BUF_SIZE);
		histogram selector_history_buf_histogram(MAX_SELECTOR_HISTORY_BUF_SIZE);

		uint32_t total_used_selector_history_buf = 0;

		histogram delta_endpoint_histogram(r.get_total_endpoint_clusters() * 2);
		histogram delta_selector_histogram(MAX_SELECTOR_HISTORY_BUF_SIZE + r.get_total_selector_clusters() * 2 + 1);
		histogram template_histogram(basist::TOTAL_ENDPOINT_INDEX_TEMPLATES);

		const uint32_t SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH = 3;
		const uint32_t SELECTOR_HISTORY_BUF_RLE_COUNT_BITS = 6;
		const uint32_t SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL = (1 << SELECTOR_HISTORY_BUF_RLE_COUNT_BITS);

		const uint32_t SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX = r.get_total_selector_clusters() * 2;
		const uint32_t SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX = SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX + MAX_SELECTOR_HISTORY_BUF_SIZE;

		histogram selector_history_buf_rle_histogram(1 << SELECTOR_HISTORY_BUF_RLE_COUNT_BITS);

		uint32_t total_selector_indices_remapped = 0;

		std::vector<uint_vec> selector_syms(m_slices.size());

		m_output.m_slice_image_crcs.resize(m_slices.size());

		for (uint32_t slice_index = 0; slice_index < m_slices.size(); slice_index++)
		{
			const uint32_t first_block_index = m_slices[slice_index].m_first_block_index;
			const uint32_t width = m_slices[slice_index].m_width;
			const uint32_t height = m_slices[slice_index].m_height;
			const uint32_t num_blocks_x = m_slices[slice_index].m_num_blocks_x;
			const uint32_t num_blocks_y = m_slices[slice_index].m_num_blocks_y;
			const uint32_t num_macroblocks_x = m_slices[slice_index].m_num_macroblocks_x;
			const uint32_t num_macroblocks_y = m_slices[slice_index].m_num_macroblocks_y;

			selector_history_buf.reset();

			int prev_endpoint_index = 0;
			int prev_selector_index = 0;
			int selector_history_buf_rle_count = 0;

			gpu_image gi;
			gi.init(cETC1, width, height);

			for (uint32_t macroblock_y = 0; macroblock_y < num_macroblocks_y; macroblock_y++)
			{
				const uint32_t y = macroblock_y * 2;

				const int x_start = (macroblock_y & 1) ? (num_macroblocks_x - 1) : 0;
				const int x_end = (macroblock_y & 1) ? -1 : num_macroblocks_x;
				const int x_dir = (macroblock_y & 1) ? -1 : 1;

				for (int macroblock_x = x_start; macroblock_x != x_end; macroblock_x += x_dir)
				{
					const uint32_t x = macroblock_x * 2;

					uint32_t block_indices[4];
					block_indices[0] = first_block_index + x + y * num_blocks_x;
					block_indices[1] = first_block_index + minimum<int>(x + 1, num_blocks_x - 1) + y * num_blocks_x;
					block_indices[2] = first_block_index + x + minimum<int>(y + 1, num_blocks_y - 1) * num_blocks_x;
					block_indices[3] = first_block_index + minimum<int>(x + 1, num_blocks_x - 1) + minimum<int>(y + 1, num_blocks_y - 1) * num_blocks_x;

					etc1_macroblock &m = m_slice_macroblocks[slice_index](macroblock_x, macroblock_y);

					template_histogram.inc(m.m_template_index);

					for (uint32_t i = 0; i < m.m_endpoint_indices.size(); i++)
					{
						int idx = m_endpoint_remap_table_old_to_new[m.m_endpoint_indices[i]];

						int delta_idx = idx - prev_endpoint_index;
						prev_endpoint_index = idx;

						m.m_endpoint_indices[i] = idx;
						m.m_endpoint_delta_indices.push_back(delta_idx);

						delta_endpoint_histogram.inc(delta_idx + r.get_total_endpoint_clusters());

						endpoint_histogram[r.get_total_endpoint_clusters() + delta_idx]++;
					}

					for (uint32_t i = 0; i < m.m_selector_indices.size(); i++)
					{
						int idx = m_selector_remap_table_old_to_new[m.m_selector_indices[i]];

						int selector_history_buf_index = -1;

#if 1
						if (m_params.m_delta_selector_rdo_quality_thresh > 0.0f)
						{
							const pixel_block &src_pixels = r.get_source_pixel_block(block_indices[i]);

							etc_block etc_blk(r.get_output_block(block_indices[i]));

							color_rgba etc_blk_unpacked[16];
							unpack_etc1(etc_blk, etc_blk_unpacked);

							uint64_t cur_err = 0;
							for (uint32_t p = 0; p < 16; p++)
								cur_err += color_distance(r.get_params().m_perceptual, src_pixels.get_ptr()[p], etc_blk_unpacked[p], false);

							uint64_t best_trial_err = UINT64_MAX;
							int best_trial_idx = 0;
							uint32_t best_trial_history_buf_idx = 0;

							//int cur_delta_idx = idx - prev_selector_index;

							etc_block best_trial_etc_block;

							const float SELECTOR_REMAP_THRESH = maximum(1.0f, m_params.m_delta_selector_rdo_quality_thresh); //2.5f;

							for (uint32_t j = 0; j < selector_history_buf.size(); j++)
							{
								int trial_idx = selector_history_buf[j];

								for (uint32_t sy = 0; sy < 4; sy++)
									for (uint32_t sx = 0; sx < 4; sx++)
										etc_blk.set_selector(sx, sy, m_selector_palette[m_selector_remap_table_new_to_old[trial_idx]](sx, sy));

								unpack_etc1(etc_blk, etc_blk_unpacked);

								uint64_t trial_err = 0;
								for (uint32_t p = 0; p < 16; p++)
									trial_err += color_distance(r.get_params().m_perceptual, src_pixels.get_ptr()[p], etc_blk_unpacked[p], false);

								if (trial_err <= cur_err * SELECTOR_REMAP_THRESH)
								{
									//int trial_delta_idx = trial_idx - prev_selector_index;

									if (trial_err < best_trial_err)
									{
										best_trial_err = trial_err;
										best_trial_idx = trial_idx;
										best_trial_etc_block = etc_blk;
										best_trial_history_buf_idx = j;
									}
								}
							}

							if (best_trial_err != UINT64_MAX)
							{
								idx = best_trial_idx;

								//total_selector_indices_remapped++;

								total_used_selector_history_buf++;

								selector_history_buf_index = best_trial_history_buf_idx;

								selector_history_buf_histogram.inc(best_trial_history_buf_idx);
							}
						}
#endif

#if 1
						if ((selector_history_buf_index < 0) && (m_params.m_delta_selector_rdo_quality_thresh > 0.0f))
						{
							const pixel_block &src_pixels = r.get_source_pixel_block(block_indices[i]);

							etc_block etc_blk(r.get_output_block(block_indices[i]));

							color_rgba etc_blk_unpacked[16];
							unpack_etc1(etc_blk, etc_blk_unpacked);

							uint64_t cur_err = 0;
							for (uint32_t p = 0; p < 16; p++)
								cur_err += color_distance(r.get_params().m_perceptual, src_pixels.get_ptr()[p], etc_blk_unpacked[p], false);

							uint64_t best_trial_err = UINT64_MAX;
							int best_trial_idx = 0;

							int cur_delta_idx = idx - prev_selector_index;

							etc_block best_trial_etc_block;

							const float SELECTOR_REMAP_THRESH = maximum(1.0f, m_params.m_delta_selector_rdo_quality_thresh); //2.5f;

							for (int d = -cur_delta_idx + 1; d < cur_delta_idx; d++)
							{
								int trial_idx = prev_selector_index + d;
								if (trial_idx < 0)
									continue;
								else if (trial_idx >= static_cast<int>(r.get_total_selector_clusters()))
									continue;

								if (trial_idx == idx)
									continue;

								//etc_blk.set_raw_selector_bits(r.get_selector_cluster_selector_bits(m_selector_remap_table_new_to_old[trial_idx]).get_raw_selector_bits());
								for (uint32_t sy = 0; sy < 4; sy++)
									for (uint32_t sx = 0; sx < 4; sx++)
										etc_blk.set_selector(sx, sy, m_selector_palette[m_selector_remap_table_new_to_old[trial_idx]](sx, sy));

								unpack_etc1(etc_blk, etc_blk_unpacked);

								uint64_t trial_err = 0;
								for (uint32_t p = 0; p < 16; p++)
									trial_err += color_distance(r.get_params().m_perceptual, src_pixels.get_ptr()[p], etc_blk_unpacked[p], false);

								if (trial_err < cur_err * SELECTOR_REMAP_THRESH)
								{
									int trial_delta_idx = trial_idx - prev_selector_index;

									const int N = r.get_total_selector_clusters() / 4;
									if (iabs(trial_delta_idx) < (uint32_t)N)
									{
										float f = iabs(trial_delta_idx) / float(N);

										f = powf(f, 2.0f);

										trial_err = static_cast<uint64_t>(trial_err * lerp(.4f, 1.0f, f));
									}

									if (trial_err < best_trial_err)
									{
										best_trial_err = trial_err;
										best_trial_idx = trial_idx;
										best_trial_etc_block = etc_blk;
									}
								}
							}

							if (best_trial_err != UINT64_MAX)
							{
								idx = best_trial_idx;

								total_selector_indices_remapped++;
							}
						} // if (m_params.m_delta_selector_rdo_quality_thresh >= 1.0f)
#endif

						int delta_idx = idx - prev_selector_index;
						prev_selector_index = idx;

						m.m_selector_indices[i] = m_selector_remap_table_new_to_old[idx];

						if ((selector_history_buf_rle_count) && (selector_history_buf_index != 0))
						{
							if (selector_history_buf_rle_count >= (int)SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH)
							{
								selector_syms[slice_index].push_back(SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX);
								selector_syms[slice_index].push_back(selector_history_buf_rle_count);

								int run_sym = selector_history_buf_rle_count - SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH;
								if (run_sym >= ((int)SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1))
									selector_history_buf_rle_histogram.inc(SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1);
								else
									selector_history_buf_rle_histogram.inc(run_sym);

								delta_selector_histogram.inc(SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX);
							}
							else
							{
								for (int k = 0; k < selector_history_buf_rle_count; k++)
								{
									uint32_t sym_index = SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX + 0;

									selector_syms[slice_index].push_back(sym_index);

									delta_selector_histogram.inc(sym_index);
								}
							}

							selector_history_buf_rle_count = 0;
						}

						if (selector_history_buf_index >= 0)
						{
							if (selector_history_buf_index == 0)
								selector_history_buf_rle_count++;
							else
							{
								uint32_t delta_indices_sym = SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX + selector_history_buf_index;

								selector_syms[slice_index].push_back(delta_indices_sym);

								delta_selector_histogram.inc(delta_indices_sym);
							}
						}
						else
						{
							uint32_t delta_indices_sym = delta_idx + r.get_total_selector_clusters();

							selector_syms[slice_index].push_back(delta_indices_sym);

							delta_selector_histogram.inc(delta_indices_sym);
						}

						m.m_selector_delta_indices.push_back(delta_idx);
						m.m_selector_history_buf_indices.push_back(selector_history_buf_index);

						actual_selector_histogram[idx]++;
						selector_histogram[r.get_total_selector_clusters() + delta_idx]++;

						if (selector_history_buf_index < 0)
							selector_history_buf.add(idx);
						else if (selector_history_buf.size())
							selector_history_buf.use(selector_history_buf_index);
					}

					for (uint32_t i = 0; i < 4; i++)
					{
						const uint32_t block_x = macroblock_x * 2 + (i & 1);
						const uint32_t block_y = macroblock_y * 2 + (i / 2);
						if ((block_x >= gi.get_blocks_x()) || (block_y >= gi.get_blocks_y()))
							continue;

						etc_block &output_block = *(etc_block *)gi.get_block_ptr(block_x, block_y);

						output_block.set_diff_bit(((m.m_diff_bits << i) & 8) != 0);
						output_block.set_flip_bit(((m.m_flip_bits << i) & 8) != 0);

						const basist::endpoint_index_template &t = basist::g_endpoint_index_templates[m.m_template_index];

						uint32_t e0 = m_endpoint_remap_table_new_to_old[m.m_endpoint_indices[t.m_local_indices[i * 2 + 0]]];
						uint32_t e1 = m_endpoint_remap_table_new_to_old[m.m_endpoint_indices[t.m_local_indices[i * 2 + 1]]];

						if (output_block.get_diff_bit())
						{
							BASISU_BACKEND_VERIFY(m_endpoint_palette[e0].m_color5_valid);
							BASISU_BACKEND_VERIFY(m_endpoint_palette[e1].m_color5_valid);

							if (!output_block.set_block_color5_check(m_endpoint_palette[e0].m_color5, m_endpoint_palette[e1].m_color5))
							{
								BASISU_BACKEND_VERIFY(0);
							}

							output_block.set_inten_table(0, m_endpoint_palette[e0].m_inten5);
							output_block.set_inten_table(1, m_endpoint_palette[e1].m_inten5);
						}
						else
						{
							BASISU_BACKEND_VERIFY(false);
						}

						uint32_t selector_idx = m.m_selector_indices[i];
						const basist::etc1_selector_palette_entry &selectors = m_selector_palette[selector_idx];
						for (uint32_t sy = 0; sy < 4; sy++)
							for (uint32_t sx = 0; sx < 4; sx++)
								output_block.set_selector(sx, sy, selectors(sx, sy));
					}

				} // macroblock_x

			} // macroblock_y

			if (selector_history_buf_rle_count)
			{
				if (selector_history_buf_rle_count >= (int)SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH)
				{
					selector_syms[slice_index].push_back(SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX);
					selector_syms[slice_index].push_back(selector_history_buf_rle_count);

					int run_sym = selector_history_buf_rle_count - SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH;
					if (run_sym >= ((int)SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1))
						selector_history_buf_rle_histogram.inc(SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1);
					else
						selector_history_buf_rle_histogram.inc(run_sym);

					delta_selector_histogram.inc(SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX);
				}
				else
				{
					for (int i = 0; i < selector_history_buf_rle_count; i++)
					{
						uint32_t sym_index = SELECTOR_HISTORY_BUF_FIRST_SYMBOL_INDEX + 0;

						selector_syms[slice_index].push_back(sym_index);

						delta_selector_histogram.inc(sym_index);
					}
				}

				selector_history_buf_rle_count = 0;
			}
						
			m_output.m_slice_image_crcs[slice_index] = basist::crc16(gi.get_ptr(), gi.get_size_in_bytes(), 0);

			if (m_params.m_debug_images)
			{
				image gi_unpacked;
				gi.unpack(gi_unpacked);

				char buf[256];
#ifdef _WIN32				
				sprintf_s(buf, sizeof(buf), "basisu_backend_slice_%u.png", slice_index);
#else
				snprintf(buf, sizeof(buf), "basisu_backend_slice_%u.png", slice_index);
#endif				
				save_png(buf, gi_unpacked);
			}

		} // slice_index

		debug_printf("Total selector indices remapped: %u %3.2f%%, Used history buf: %u %3.2f%%\n",
			total_selector_indices_remapped, total_selector_indices_remapped * 100.0f / (get_total_macroblocks() * 4),
			total_used_selector_history_buf, total_used_selector_history_buf * 100.0f / (get_total_macroblocks() * 4));

		if (m_params.m_debug_images)
		{
			//draw_histogram_chart("delta_endpoint_hist.png", "Delta Endpoint Histogram", endpoint_histogram);
			//draw_histogram_chart("delta_selector_hist.png", "Delta Selector Histogram", selector_histogram);
			//draw_histogram_chart("selector_hist.png", "Selector Histogram", actual_selector_histogram);
		}

		double delta_endpoint_entropy = delta_endpoint_histogram.get_entropy() / delta_endpoint_histogram.get_total();
		double delta_selector_entropy = delta_selector_histogram.get_entropy() / delta_selector_histogram.get_total();
		double template_entropy = template_histogram.get_entropy() / template_histogram.get_total();

		debug_printf("Entropy: AvgEndpoints/macroblock: %3.3f DeltaEndpoint: %3.3f DeltaSelector: %3.3f Template: %3.3f\n",
			static_cast<double>(delta_endpoint_histogram.get_total()) / get_total_macroblocks(),
			delta_endpoint_entropy, delta_selector_entropy, template_entropy);

		huffman_encoding_table template_model;
		if (!template_model.init(template_histogram, 16))
		{
			error_printf("template_model.init() failed!");
			return false;
		}

		huffman_encoding_table delta_endpoint_model;
		if (!delta_endpoint_model.init(delta_endpoint_histogram, 16))
		{
			error_printf("delta_endpoint_model.init() failed!");
			return false;
		}

		BASISU_ASSUME(basisu_frontend::cMaxEndpointClusterBits <= 15);
		uint32_t max_delta_selector_code_size = ceil_log2i(r.get_total_selector_clusters() * 2) + 2;

		max_delta_selector_code_size = clamp<int>(max_delta_selector_code_size, 10, 15);

		if (m_params.m_debug_images)
		{
			uint_vec delta_selector_plot_histogram(delta_selector_histogram.size());
			for (uint32_t i = 0; i < delta_selector_histogram.size(); i++)
				delta_selector_plot_histogram[i] = delta_selector_histogram[i];
			//draw_histogram_chart("delta_selector_symbol_hist.png", "Delta Selector Symbol Histogram", delta_selector_plot_histogram);
		}

		huffman_encoding_table delta_selector_model;
		if (!delta_selector_model.init(delta_selector_histogram, max_delta_selector_code_size))
		{
			error_printf("delta_selector_model.init() failed!");
			return false;
		}

		if (!selector_history_buf_rle_histogram.get_total())
			selector_history_buf_rle_histogram.inc(0);

		huffman_encoding_table selector_history_buf_rle_model;
		if (!selector_history_buf_rle_model.init(selector_history_buf_rle_histogram, 15))
		{
			error_printf("selector_history_buf_rle_model.init() failed!");
			return false;
		}

		bitwise_coder coder;
		coder.init(1024 * 1024 * 4);

		uint32_t template_model_bits = coder.emit_huffman_table(template_model);
		uint32_t delta_endpoint_model_bits = coder.emit_huffman_table(delta_endpoint_model);
		uint32_t delta_selector_model_bits = coder.emit_huffman_table(delta_selector_model);
		uint32_t selector_history_buf_run_sym_bits = coder.emit_huffman_table(selector_history_buf_rle_model);

		coder.put_bits(MAX_SELECTOR_HISTORY_BUF_SIZE, 13);
				
		const uint32_t SELECTOR_HISTORY_BUF_RUN_RICE_BITS = 3;
		coder.put_bits(SELECTOR_HISTORY_BUF_RUN_RICE_BITS, 4);

		debug_printf("Model sizes: Template: %u DeltaEndpoint: %u (%3.3f bpp) DeltaSelector: %u (%3.3f bpp) SelectorHistBufRLE: %u (%3.3f bpp)\n",
			(template_model_bits + 7) / 8,
			(delta_endpoint_model_bits + 7) / 8, delta_endpoint_model_bits / float(get_total_input_texels()),
			(delta_selector_model_bits + 7) / 8, delta_selector_model_bits / float(get_total_input_texels()),
			(selector_history_buf_run_sym_bits + 7) / 8, selector_history_buf_run_sym_bits / float(get_total_input_texels()));

		coder.flush();

		m_output.m_slice_image_tables = coder.get_bytes();

		uint32_t total_template_bits = 0, total_delta_endpoint_bits = 0, total_delta_selector_bits = 0;

		uint32_t total_image_bytes = 0;

		m_output.m_slice_image_data.resize(m_slices.size());

		for (uint32_t slice_index = 0; slice_index < m_slices.size(); slice_index++)
		{
			const uint32_t width = m_slices[slice_index].m_width;
			const uint32_t height = m_slices[slice_index].m_height;
			const uint32_t num_blocks_x = m_slices[slice_index].m_num_blocks_x;
			const uint32_t num_blocks_y = m_slices[slice_index].m_num_blocks_y;
			const uint32_t num_macroblocks_x = m_slices[slice_index].m_num_macroblocks_x;
			const uint32_t num_macroblocks_y = m_slices[slice_index].m_num_macroblocks_y;

			coder.init(1024 * 1024 * 4);

			uint32_t cur_selector_sym_ofs = 0;
			uint32_t selector_rle_count = 0;

			for (uint32_t macroblock_y = 0; macroblock_y < num_macroblocks_y; macroblock_y++)
			{
				const int x_start = (macroblock_y & 1) ? (num_macroblocks_x - 1) : 0;
				const int x_end = (macroblock_y & 1) ? -1 : num_macroblocks_x;
				const int x_dir = (macroblock_y & 1) ? -1 : 1;

				for (int macroblock_x = x_start; macroblock_x != x_end; macroblock_x += x_dir)
				{
					const etc1_macroblock &m = m_slice_macroblocks[slice_index](macroblock_x, macroblock_y);

					total_template_bits += coder.put_code(m.m_template_index, template_model);
					
					for (uint32_t i = 0; i < m.m_endpoint_delta_indices.size(); i++)
						total_delta_endpoint_bits += coder.put_code(m.m_endpoint_delta_indices[i] + r.get_total_endpoint_clusters(), delta_endpoint_model);

					for (uint32_t i = 0; i < 4; i++)
					{
						if (!selector_rle_count)
						{
							uint32_t selector_sym_index = selector_syms[slice_index][cur_selector_sym_ofs++];

							if (selector_sym_index == SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX)
								selector_rle_count = selector_syms[slice_index][cur_selector_sym_ofs++];

							total_delta_selector_bits += coder.put_code(selector_sym_index, delta_selector_model);

							if (selector_sym_index == SELECTOR_HISTORY_BUF_RLE_SYMBOL_INDEX)
							{
								int run_sym = selector_rle_count - SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH;
								if (run_sym >= ((int)SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1))
								{
									total_delta_selector_bits += coder.put_code(SELECTOR_HISTORY_BUF_RLE_COUNT_TOTAL - 1, selector_history_buf_rle_model);
									total_delta_selector_bits += coder.put_rice(selector_rle_count - SELECTOR_HISTORY_BUF_RLE_COUNT_THRESH, SELECTOR_HISTORY_BUF_RUN_RICE_BITS);
								}
								else
									total_delta_selector_bits += coder.put_code(run_sym, selector_history_buf_rle_model);
							}
						}

						if (selector_rle_count)
							selector_rle_count--;
					}

				} // macroblock_x

			} // macroblock_y

			BASISU_BACKEND_VERIFY(cur_selector_sym_ofs == selector_syms[slice_index].size());

			coder.flush();

			m_output.m_slice_image_data[slice_index] = coder.get_bytes();

			total_image_bytes += (uint32_t)coder.get_bytes().size();

			debug_printf("Slice %u compressed size: %u bytes, %3.3f bits per slice texel\n", slice_index, m_output.m_slice_image_data[slice_index].size(), m_output.m_slice_image_data[slice_index].size() * 8.0f / (m_slices[slice_index].m_orig_width * m_slices[slice_index].m_orig_height));

		} // slice_index

		const double total_texels = static_cast<double>(get_total_input_texels());
		const double total_macroblocks = static_cast<double>(get_total_macroblocks());

		debug_printf("Total template bits: %u bytes: %u bits/texel: %3.3f bits/macroblock: %3.3f\n", total_template_bits, total_template_bits / 8, total_template_bits / total_texels, total_template_bits / total_macroblocks);
		debug_printf("Total delta endpoint bits: %u bytes: %u bits/texel: %3.3f bits/macroblock: %3.3f\n", total_delta_endpoint_bits, total_delta_endpoint_bits / 8, total_delta_endpoint_bits / total_texels, total_delta_endpoint_bits / total_macroblocks);
		debug_printf("Total delta selector bits: %u bytes: %u bits/texel: %3.3f bits/macroblock: %3.3f\n", total_delta_selector_bits, total_delta_selector_bits / 8, total_delta_selector_bits / total_texels, total_delta_selector_bits / total_macroblocks);

		debug_printf("Total table bytes: %u, Total image bytes: %u, %3.3f bits/texel\n", m_output.m_slice_image_tables.size(), total_image_bytes, total_image_bytes * 8.0f / total_texels);

		return true;
	}

	bool basisu_backend::encode_endpoint_palette()
	{
		const basisu_frontend &r = *m_pFront_end;
				
		histogram color5_delta_hist(32 * 2 - 1);
		histogram inten5_delta_hist(8 * 2 - 1);
		
		color_rgba prev_color5(0, 0, 0, 0);
		int prev_inten5 = 0;

		// Maps NEW to OLD endpoints
		uint_vec endpoint_remap_table_inv(r.get_total_endpoint_clusters());
		for (uint32_t old_endpoint_index = 0; old_endpoint_index < m_endpoint_remap_table_old_to_new.size(); old_endpoint_index++)
			endpoint_remap_table_inv[m_endpoint_remap_table_old_to_new[old_endpoint_index]] = old_endpoint_index;

		for (uint32_t new_endpoint_index = 0; new_endpoint_index < r.get_total_endpoint_clusters(); new_endpoint_index++)
		{
			const uint32_t old_endpoint_index = endpoint_remap_table_inv[new_endpoint_index];
												
			int delta_r5 = m_endpoint_palette[old_endpoint_index].m_color5[0] - prev_color5[0];
			int delta_g5 = m_endpoint_palette[old_endpoint_index].m_color5[1] - prev_color5[1];
			int delta_b5 = m_endpoint_palette[old_endpoint_index].m_color5[2] - prev_color5[2];
			int delta_inten5 = m_endpoint_palette[old_endpoint_index].m_inten5 - prev_inten5;

			prev_color5[0] = m_endpoint_palette[old_endpoint_index].m_color5[0];
			prev_color5[1] = m_endpoint_palette[old_endpoint_index].m_color5[1];
			prev_color5[2] = m_endpoint_palette[old_endpoint_index].m_color5[2];
			prev_inten5 = m_endpoint_palette[old_endpoint_index].m_inten5;

			color5_delta_hist.inc(31 + delta_r5);
			color5_delta_hist.inc(31 + delta_g5);
			color5_delta_hist.inc(31 + delta_b5);
			inten5_delta_hist.inc(7 + delta_inten5);
		}
				
		huffman_encoding_table color5_delta_model;
		if (!color5_delta_model.init(color5_delta_hist, 16))
		{
			error_printf("color5_delta_model.init() failed!");
			return false;
		}

		huffman_encoding_table inten5_delta_model;
		if (!inten5_delta_model.init(inten5_delta_hist, 16))
		{
			error_printf("inten5_delta_model.init() failed!");
			return false;
		}
								
		bitwise_coder coder;

		coder.init(1024 * 1024);

		coder.emit_huffman_table(color5_delta_model);
		coder.emit_huffman_table(inten5_delta_model);
		
		prev_color5.set(0, 0, 0, 0);
		prev_inten5 = 0;

		for (uint32_t q = 0; q < r.get_total_endpoint_clusters(); q++)
		{
			const uint32_t i = endpoint_remap_table_inv[q];
						
			int delta_r5 = m_endpoint_palette[i].m_color5[0] - prev_color5[0];
			int delta_g5 = m_endpoint_palette[i].m_color5[1] - prev_color5[1];
			int delta_b5 = m_endpoint_palette[i].m_color5[2] - prev_color5[2];
			int delta_inten5 = m_endpoint_palette[i].m_inten5 - prev_inten5;

			prev_color5[0] = m_endpoint_palette[i].m_color5[0];
			prev_color5[1] = m_endpoint_palette[i].m_color5[1];
			prev_color5[2] = m_endpoint_palette[i].m_color5[2];
			prev_inten5 = m_endpoint_palette[i].m_inten5;

			coder.put_code(31 + delta_r5, color5_delta_model);
			coder.put_code(31 + delta_g5, color5_delta_model);
			coder.put_code(31 + delta_b5, color5_delta_model);
			coder.put_code(7 + delta_inten5, inten5_delta_model);
			
		} // q

		coder.flush();

		m_output.m_endpoint_palette = coder.get_bytes();

		debug_printf("Endpoint palette size: %u, Bits per entry: %3.1f, Avg bits/texel: %3.3f\n",
			m_output.m_endpoint_palette.size(), m_output.m_endpoint_palette.size() * 8.0f / r.get_total_endpoint_clusters(), m_output.m_endpoint_palette.size() * 8.0f / get_total_input_texels());

		return true;
	}

	bool basisu_backend::encode_selector_palette()
	{
		const basisu_frontend &r = *m_pFront_end;

		if ((m_params.m_use_global_sel_codebook) && (!m_params.m_use_hybrid_sel_codebooks))
		{
			histogram global_mod_indices(1 << m_params.m_global_sel_codebook_mod_bits);

			for (uint32_t q = 0; q < r.get_total_selector_clusters(); q++)
				global_mod_indices.inc(m_global_selector_palette_desc[q].m_mod_index);

			huffman_encoding_table global_pal_model, global_mod_model;

			if (!global_mod_model.init(global_mod_indices, 16))
			{
				error_printf("global_mod_model.init() failed!");
				return false;
			}

			bitwise_coder coder;
			coder.init(1024 * 1024);

			coder.put_bits(1, 1); // use global codebook

			coder.put_bits(m_params.m_global_sel_codebook_pal_bits, 4); // pal bits
			coder.put_bits(m_params.m_global_sel_codebook_mod_bits, 4); // mod bits

			uint32_t mod_model_bits = 0;
			if (m_params.m_global_sel_codebook_mod_bits)
				mod_model_bits = coder.emit_huffman_table(global_mod_model);

			uint32_t total_pal_bits = 0;
			uint32_t total_mod_bits = 0;
			for (uint32_t q = 0; q < r.get_total_selector_clusters(); q++)
			{
				const uint32_t i = m_selector_remap_table_new_to_old[q];

				if (m_params.m_global_sel_codebook_pal_bits)
				{
					coder.put_bits(m_global_selector_palette_desc[i].m_pal_index, m_params.m_global_sel_codebook_pal_bits);
					total_pal_bits += m_params.m_global_sel_codebook_pal_bits;
				}

				if (m_params.m_global_sel_codebook_mod_bits)
					total_mod_bits += coder.put_code(m_global_selector_palette_desc[i].m_mod_index, global_mod_model);
			}

			coder.flush();

			m_output.m_selector_palette = coder.get_bytes();

			debug_printf("Modifier model bits: %u Avg per entry: %3.3f\n", mod_model_bits, mod_model_bits / float(r.get_total_selector_clusters()));
			debug_printf("Palette bits: %u Avg per entry: %3.3f, Modifier bits: %u Avg per entry: %3.3f\n", total_pal_bits, total_pal_bits / float(r.get_total_selector_clusters()), total_mod_bits, total_mod_bits / float(r.get_total_selector_clusters()));
		}
		else if (m_params.m_use_hybrid_sel_codebooks)
		{
			huff2D used_global_cb_bitflag_huff2D(1, 8);

			histogram global_mod_indices(1 << m_params.m_global_sel_codebook_mod_bits);

			for (uint32_t s = 0; s < r.get_total_selector_clusters(); s++)
			{
				const uint32_t q = m_selector_remap_table_new_to_old[s];

				const bool used_global_cb_flag = r.get_selector_cluster_uses_global_cb_vec()[q];

				used_global_cb_bitflag_huff2D.emit(used_global_cb_flag);

				global_mod_indices.inc(m_global_selector_palette_desc[q].m_mod_index);
			}

			huffman_encoding_table global_mod_indices_model;
			if (!global_mod_indices_model.init(global_mod_indices, 16))
			{
				error_printf("global_mod_indices_model.init() failed!");
				return false;
			}

			bitwise_coder coder;
			coder.init(1024 * 1024);

			coder.put_bits(0, 1); // use global codebook
			coder.put_bits(1, 1); // uses hybrid codebooks

			coder.put_bits(m_params.m_global_sel_codebook_pal_bits, 4); // pal bits
			coder.put_bits(m_params.m_global_sel_codebook_mod_bits, 4); // mod bits

			used_global_cb_bitflag_huff2D.start_encoding(16);
			coder.emit_huffman_table(used_global_cb_bitflag_huff2D.get_encoding_table());

			if (m_params.m_global_sel_codebook_mod_bits)
				coder.emit_huffman_table(global_mod_indices_model);

			uint32_t total_global_cb_entries = 0;
			uint32_t total_pal_bits = 0;
			uint32_t total_mod_bits = 0;
			uint32_t total_selectors = 0;
			uint32_t total_selector_bits = 0;
			uint32_t total_flag_bits = 0;

			for (uint32_t s = 0; s < r.get_total_selector_clusters(); s++)
			{
				const uint32_t q = m_selector_remap_table_new_to_old[s];

				total_flag_bits += used_global_cb_bitflag_huff2D.emit_next_sym(coder);

				const bool used_global_cb_flag = r.get_selector_cluster_uses_global_cb_vec()[q];

				if (used_global_cb_flag)
				{
					total_global_cb_entries++;

					total_pal_bits += coder.put_bits(r.get_selector_cluster_global_selector_entry_ids()[q].m_palette_index, m_params.m_global_sel_codebook_pal_bits);
					total_mod_bits += coder.put_code(r.get_selector_cluster_global_selector_entry_ids()[q].m_modifier.get_index(), global_mod_indices_model);
				}
				else
				{
					total_selectors++;
					total_selector_bits += 32;

					for (uint32_t j = 0; j < 4; j++)
						coder.put_bits(m_selector_palette[q].get_byte(j), 8);
				}
			}

			coder.flush();

			m_output.m_selector_palette = coder.get_bytes();

			debug_printf("Total global CB entries: %u %3.2f%%\n", total_global_cb_entries, total_global_cb_entries * 100.0f / r.get_total_selector_clusters());
			debug_printf("Total selector entries: %u %3.2f%%\n", total_selectors, total_selectors * 100.0f / r.get_total_selector_clusters());
			debug_printf("Total pal bits: %u, mod bits: %u, selector bits: %u, flag bits: %u\n", total_pal_bits, total_mod_bits, total_selector_bits, total_flag_bits);
		}
		else
		{
			histogram delta_selector_pal_histogram(256);

			for (uint32_t q = 0; q < r.get_total_selector_clusters(); q++)
			{
				if (!q)
					continue;

				const basist::etc1_selector_palette_entry &cur = m_selector_palette[m_selector_remap_table_new_to_old[q]];
				const basist::etc1_selector_palette_entry predictor(m_selector_palette[m_selector_remap_table_new_to_old[q - 1]]);

				for (uint32_t j = 0; j < 4; j++)
					delta_selector_pal_histogram.inc(cur.get_byte(j) ^ predictor.get_byte(j));
			}

			if (!delta_selector_pal_histogram.get_total())
				delta_selector_pal_histogram.inc(0);

			huffman_encoding_table delta_selector_pal_model;
			if (!delta_selector_pal_model.init(delta_selector_pal_histogram, 16))
			{
				error_printf("delta_selector_pal_model.init() failed!");
				return false;
			}

			bitwise_coder coder;
			coder.init(1024 * 1024);

			coder.put_bits(0, 1); // use global codebook
			coder.put_bits(0, 1); // uses hybrid codebooks

			coder.put_bits(0, 1); // raw bytes

			coder.emit_huffman_table(delta_selector_pal_model);

			for (uint32_t q = 0; q < r.get_total_selector_clusters(); q++)
			{
				if (!q)
				{
					for (uint32_t j = 0; j < 4; j++)
						coder.put_bits(m_selector_palette[m_selector_remap_table_new_to_old[q]].get_byte(j), 8);
					continue;
				}

				const basist::etc1_selector_palette_entry &cur = m_selector_palette[m_selector_remap_table_new_to_old[q]];
				const basist::etc1_selector_palette_entry predictor(m_selector_palette[m_selector_remap_table_new_to_old[q - 1]]);

				for (uint32_t j = 0; j < 4; j++)
					coder.put_code(cur.get_byte(j) ^ predictor.get_byte(j), delta_selector_pal_model);
			}

			coder.flush();

			m_output.m_selector_palette = coder.get_bytes();

			if (m_output.m_selector_palette.size() >= r.get_total_selector_clusters() * 4)
			{
				coder.init(1024 * 1024);

				coder.put_bits(0, 1); // use global codebook
				coder.put_bits(0, 1); // uses hybrid codebooks

				coder.put_bits(1, 1); // raw bytes

				for (uint32_t q = 0; q < r.get_total_selector_clusters(); q++)
				{
					const uint32_t i = m_selector_remap_table_new_to_old[q];

					for (uint32_t j = 0; j < 4; j++)
						coder.put_bits(m_selector_palette[i].get_byte(j), 8);
				}

				coder.flush();

				m_output.m_selector_palette = coder.get_bytes();
			}

		}  // if (m_params.m_use_global_sel_codebook)        

		debug_printf("Selector palette bytes: %u, Bits per entry: %3.1f, Avg bits/texel: %3.3f\n", m_output.m_selector_palette.size(), m_output.m_selector_palette.size() * 8.0f / r.get_total_selector_clusters(), m_output.m_selector_palette.size() * 8.0f / get_total_input_texels());

		return true;
	}

	uint32_t basisu_backend::encode()
	{
		const basisu_frontend &r = *m_pFront_end;

		m_output.m_slice_desc = m_slices;
		m_output.m_etc1s = m_params.m_etc1s;
		m_output.m_num_endpoints = r.get_total_endpoint_clusters();
		m_output.m_num_selectors = r.get_total_selector_clusters();

		create_endpoint_palette();
		create_selector_palette();

		create_macroblocks();

		if (!encode_image())
			return 0;

		if (!encode_endpoint_palette())
			return 0;

		if (!encode_selector_palette())
			return 0;

		uint32_t total_compressed_bytes = (uint32_t)(m_output.m_slice_image_tables.size() + m_output.m_endpoint_palette.size() + m_output.m_selector_palette.size());
		for (uint32_t i = 0; i < m_output.m_slice_image_data.size(); i++)
			total_compressed_bytes += (uint32_t)m_output.m_slice_image_data[i].size();

		debug_printf("Wrote %u bytes, %3.3f bits/texel\n", total_compressed_bytes, total_compressed_bytes * 8.0f / get_total_input_texels());

		return total_compressed_bytes;
	}

} // namespace basisu
