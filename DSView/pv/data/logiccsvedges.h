// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DSVIEW_PV_DATA_LOGIC_CSV_EDGES_H
#define DSVIEW_PV_DATA_LOGIC_CSV_EDGES_H

#include <cstdint>
#include <cstring>
#include <vector>

namespace pv { namespace data {
// Channels are bit-packed, least significant bit first. Preserve state across
// calls, including across snapshot blocks and export chunks.
struct CsvEdgeScanner {
    uint64_t previous = 0;
    bool initialized = false;

    template<typename Emit>
    void scan(const std::vector<uint8_t *> &channels,
              const std::vector<bool> &constant_high,
              uint64_t first, uint64_t count, Emit on_edge)
    {
        for (uint64_t pos = first; pos < first + count;) {
            const uint64_t n = (first + count - pos >= 64) ? 64 : first + count - pos;
            if (n == 64 && pos % 8 == 0) {
                uint64_t words[64];
                uint64_t changes = 0;
                for (size_t c = 0; c < channels.size(); ++c) {
                    if (channels[c]) std::memcpy(&words[c], channels[c] + pos / 8, 8);
                    else words[c] = constant_high[c] ? ~uint64_t(0) : 0;
                    const uint64_t prev = (previous >> c) & 1;
                    changes |= words[c] ^ ((words[c] << 1) | prev);
                }
                if (!initialized) changes |= 1;
                while (changes) {
#if defined(__GNUC__) || defined(__clang__)
                    unsigned bit = static_cast<unsigned>(__builtin_ctzll(changes));
#else
                    unsigned bit = 0; while (((changes >> bit) & 1) == 0) ++bit;
#endif
                    uint64_t value = 0;
                    for (size_t c = 0; c < channels.size(); ++c)
                        value |= ((words[c] >> bit) & 1) << c;
                    on_edge(pos + bit, value);
                    previous = value; initialized = true;
                    changes &= changes - 1;
                }
                pos += n; continue;
            }
            for (uint64_t j = 0; j < n; ++j) {
                uint64_t value = 0;
                for (size_t c = 0; c < channels.size(); ++c) {
                    const bool bit = channels[c] ?
                        ((channels[c][(pos+j)/8] >> ((pos+j)%8)) & 1) : constant_high[c];
                    if (bit) value |= uint64_t(1) << c;
                }
                if (!initialized || value != previous) {
                    on_edge(pos + j, value);
                    previous = value;
                    initialized = true;
                }
            }
            pos += n;
        }
    }
};
}}
#endif
