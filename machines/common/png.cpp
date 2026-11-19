// PNG writer: signature, IHDR, PLTE when the picture has 256 colours or
// fewer, one IDAT, IEND.  Format: RFC 2083 (PNG), RFC 1950 (zlib), RFC 1951
// (deflate).
//
// The compressor is deliberately small: one deflate block with the fixed
// Huffman codes, looking for repeats in only two places, the byte before
// (runs of one colour) and the byte above (rows that repeat).  That suits
// pictures drawn by a computer and cuts them to a few percent.
#include "png.h"

namespace basic {

namespace {

uint32_t crc_table[256];

void make_crc_table()
{
    for(uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for(int k = 0; k < 8; k++) { c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
        crc_table[n] = c;
    }
}

void put32(Vec<char> &out, uint32_t v)
{
    for(int shift = 24; shift >= 0; shift -= 8) { out.push(static_cast<char>(v >> shift)); }
}

// A chunk: length, type, data, CRC of type and data.
void chunk(Vec<char> &out, const char *type, const Vec<char> &data)
{
    put32(out, data.n);
    uint32_t start = out.n;
    for(int k = 0; k < 4; k++) { out.push(type[k]); }
    for(uint32_t k = 0; k < data.n; k++) { out.push(data.d[k]); }
    uint32_t crc = 0xFFFFFFFFu;
    for(uint32_t k = start; k < out.n; k++) {
        crc = crc_table[(crc ^ static_cast<uint8_t>(out.d[k])) & 0xFF] ^ (crc >> 8);
    }
    put32(out, crc ^ 0xFFFFFFFFu);
}

// Deflate output, least significant bit first.
struct Bits {
    Vec<char> &out;
    uint32_t hold;
    int count;

    void put(uint32_t value, int n)
    {
        hold |= value << count;
        count += n;
        while(count >= 8) {
            out.push(static_cast<char>(hold));
            hold >>= 8;
            count -= 8;
        }
    }
    // Huffman codes go most significant bit first.
    void code(uint32_t value, int n)
    {
        for(int k = n - 1; k >= 0; k--) { put((value >> k) & 1, 1); }
    }
    void symbol(int sym)                          // literal/length alphabet, fixed codes
    {
        if(sym < 144) {
            code(0x30 + static_cast<uint32_t>(sym), 8);
        } else if(sym < 256) {
            code(0x190 + static_cast<uint32_t>(sym - 144), 9);
        } else if(sym < 280) {
            code(static_cast<uint32_t>(sym - 256), 7);
        } else {
            code(0xC0 + static_cast<uint32_t>(sym - 280), 8);
        }
    }
    void flush()
    {
        if(count > 0) { put(0, 8 - count); }
    }
};

const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258,
};
const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
const uint16_t distance_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
const uint8_t distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

// zlib-wrap `raw` into `out`.  `stride` is the length of one scanline.
void deflate(const Vec<char> &raw, uint32_t stride, Vec<char> &out)
{
    out.push(0x78);
    out.push(0x01);
    Bits bits = {out, 0, 0};
    bits.put(1, 1);                               // last block
    bits.put(1, 2);                               // fixed Huffman codes
    uint32_t i = 0;
    while(i < raw.n) {
        uint32_t best = 0, distance = 0;
        const uint32_t places[2] = {1, stride};
        for(uint32_t d : places) {
            if(d > i || d > 32768) { continue; }
            uint32_t n = 0;
            while(n < 258 && i + n < raw.n && raw.d[i + n] == raw.d[i + n - d]) { n++; }
            if(n > best) {
                best = n;
                distance = d;
            }
        }
        if(best < 3) {
            bits.symbol(static_cast<uint8_t>(raw.d[i++]));
            continue;
        }
        int lc = 28;
        while(length_base[lc] > best) { lc--; }
        bits.symbol(257 + lc);
        bits.put(best - length_base[lc], length_extra[lc]);
        int dc = 29;
        while(distance_base[dc] > distance) { dc--; }
        bits.code(static_cast<uint32_t>(dc), 5);
        bits.put(distance - distance_base[dc], distance_extra[dc]);
        i += best;
    }
    bits.symbol(256);                             // end of block
    bits.flush();
    uint32_t a = 1, b = 0;                        // Adler-32 of the uncompressed data
    for(uint32_t k = 0; k < raw.n; k++) {
        a = (a + static_cast<uint8_t>(raw.d[k])) % 65521;
        b = (b + a) % 65521;
    }
    put32(out, (b << 16) | a);
}

}  // namespace

void png_encode(const uint32_t *pixels, int width, int height, Vec<char> &out)
{
    if(!crc_table[1]) { make_crc_table(); }
    out.clear();
    static const char signature[8] = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1A', '\n'};
    for(char c : signature) { out.push(c); }

    // The colours used, in order of first appearance, while there are few
    // enough of them for a palette.
    uint32_t palette[256];
    int colours = 0;
    for(int k = 0; k < width * height && colours <= 256; k++) {
        int c = 0;
        while(c < colours && palette[c] != pixels[k]) { c++; }
        if(c == colours) {
            if(colours < 256) { palette[c] = pixels[k]; }
            colours++;
        }
    }
    bool indexed = colours <= 256;

    Vec<char> data;
    put32(data, static_cast<uint32_t>(width));
    put32(data, static_cast<uint32_t>(height));
    data.push(8);                                 // bits per sample
    data.push(indexed ? 3 : 2);                   // colour type: palette or RGB
    data.push(0);
    data.push(0);
    data.push(0);
    chunk(out, "IHDR", data);

    if(indexed) {
        data.clear();
        for(int c = 0; c < colours; c++) {
            data.push(static_cast<char>(palette[c] >> 16));
            data.push(static_cast<char>(palette[c] >> 8));
            data.push(static_cast<char>(palette[c]));
        }
        chunk(out, "PLTE", data);
    }

    // Each scanline is a filter byte (0: none) and its pixels.
    Vec<char> raw;
    for(int y = 0; y < height; y++) {
        raw.push(0);
        int last = 0;
        for(int x = 0; x < width; x++) {
            uint32_t p = pixels[y * width + x];
            if(indexed) {
                if(palette[last] != p) {
                    for(last = 0; palette[last] != p; last++) { }
                }
                raw.push(static_cast<char>(last));
            } else {
                raw.push(static_cast<char>(p >> 16));
                raw.push(static_cast<char>(p >> 8));
                raw.push(static_cast<char>(p));
            }
        }
    }
    data.clear();
    deflate(raw, static_cast<uint32_t>(width) * (indexed ? 1 : 3) + 1, data);
    chunk(out, "IDAT", data);

    data.clear();
    chunk(out, "IEND", data);
    free(data.d);
    free(raw.d);
}

}  // namespace basic
