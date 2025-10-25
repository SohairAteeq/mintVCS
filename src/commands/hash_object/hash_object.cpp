// hash_object.cpp
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <iomanip>
#include <zlib.h>
#include <string.h>

using namespace std;
namespace fs = std::filesystem;

// Minimal, public-domain SHA-1 implementation (small, self-contained).
// Provides: void sha1_init(...), sha1_update(...), sha1_final(...)
// This is a straightforward reference implementation suitable for small projects.
struct SHA1_CTX {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
};

static inline uint32_t rol(uint32_t value, unsigned int bits) {
    return (value << bits) | (value >> (32 - bits));
}

void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(buffer[i*4]) << 24) |
               (uint32_t(buffer[i*4+1]) << 16) |
               (uint32_t(buffer[i*4+2]) << 8) |
               (uint32_t(buffer[i*4+3]));
    }
    for (int i = 16; i < 80; ++i)
        w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = rol(a,5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b,30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void sha1_init(SHA1_CTX &ctx) {
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xEFCDAB89;
    ctx.state[2] = 0x98BADCFE;
    ctx.state[3] = 0x10325476;
    ctx.state[4] = 0xC3D2E1F0;
    ctx.count = 0;
    memset(ctx.buffer, 0, sizeof(ctx.buffer));
}

void sha1_update(SHA1_CTX &ctx, const uint8_t *data, size_t len) {
    size_t i = 0;
    size_t idx = (ctx.count >> 3) & 0x3F;
    ctx.count += static_cast<uint64_t>(len) << 3;

    if (idx) {
        size_t fill = 64 - idx;
        if (len >= fill) {
            memcpy(ctx.buffer + idx, data, fill);
            sha1_transform(ctx.state, ctx.buffer);
            i += fill;
            idx = 0;
        } else {
            memcpy(ctx.buffer + idx, data, len);
            return;
        }
    }

    for (; i + 63 < len; i += 64)
        sha1_transform(ctx.state, data + i);

    if (i < len)
        memcpy(ctx.buffer, data + i, len - i);
}

void sha1_final(SHA1_CTX &ctx, uint8_t digest[20]) {
    uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    uint64_t cnt = ctx.count;
    for (int i = 0; i < 8; ++i) {
        bits[7 - i] = static_cast<uint8_t>(cnt & 0xff);
        cnt >>= 8;
    }

    size_t idx = (ctx.count >> 3) & 0x3F;
    size_t padLen = (idx < 56) ? (56 - idx) : (120 - idx);
    sha1_update(ctx, padding, padLen);
    sha1_update(ctx, bits, 8);

    for (int i = 0; i < 5; ++i) {
        digest[i*4]     = static_cast<uint8_t>((ctx.state[i] >> 24) & 0xff);
        digest[i*4 + 1] = static_cast<uint8_t>((ctx.state[i] >> 16) & 0xff);
        digest[i*4 + 2] = static_cast<uint8_t>((ctx.state[i] >> 8) & 0xff);
        digest[i*4 + 3] = static_cast<uint8_t>((ctx.state[i]) & 0xff);
    }
}

// Read entire file into vector<uint8_t>
vector<uint8_t> read_file_bytes(const string &path) {
    ifstream ifs(path, ios::binary);
    if (!ifs) throw runtime_error("Unable to open file: " + path);
    ifs.seekg(0, ios::end);
    streamoff size = ifs.tellg();
    ifs.seekg(0, ios::beg);
    vector<uint8_t> data;
    data.resize(static_cast<size_t>(size));
    if (size > 0)
        ifs.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// Build "blob <size>\0<content>" in a vector<uint8_t>
vector<uint8_t> build_blob_store(const vector<uint8_t> &content) {
    string header = "blob " + to_string(content.size()) + '\0';
    vector<uint8_t> out;
    out.reserve(header.size() + content.size());
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

// Compute SHA-1 digest of data vector and return 40-char hex OID
string sha1_hex_of_bytes(const vector<uint8_t> &data) {
    SHA1_CTX ctx;
    sha1_init(ctx);
    if (!data.empty())
        sha1_update(ctx, data.data(), data.size());
    uint8_t digest[20];
    sha1_final(ctx, digest);

    ostringstream oss;
    oss << hex << setfill('0');
    for (int i = 0; i < 20; ++i)
        oss << setw(2) << static_cast<int>(digest[i]);
    return oss.str();
}

// Compress bytes using zlib compress(); returns compressed vector
vector<uint8_t> zlib_compress_bytes(const vector<uint8_t> &in) {
    uLong source_len = static_cast<uLong>(in.size());
    uLong bound = compressBound(source_len);
    vector<uint8_t> out;
    out.resize(bound);
    int res = compress(out.data(), &bound, in.empty() ? nullptr : in.data(), source_len);
    if (res != Z_OK) throw runtime_error("zlib compress failed");
    out.resize(bound);
    return out;
}

// Write compressed object into .mintvcs/objects/xx/yyyy... ; skip if exists
void write_object_file(const string &oid_hex, const vector<uint8_t> &compressed) {
    string dir = ".mintvcs/objects/" + oid_hex.substr(0,2);
    string filename = oid_hex.substr(2);
    fs::path dpath(dir);
    fs::create_directories(dpath);
    fs::path fpath = dpath / filename;

    if (fs::exists(fpath)) return; // do not overwrite existing object

    ofstream ofs(fpath, ios::binary);
    if (!ofs) throw runtime_error("Unable to write object file: " + fpath.string());
    ofs.write(reinterpret_cast<const char*>(compressed.data()), compressed.size());
}

// main hash-object function
string hash_object(const string &filepath, bool write) {
    vector<uint8_t> content = read_file_bytes(filepath);
    vector<uint8_t> store = build_blob_store(content);
    string oid = sha1_hex_of_bytes(store);

    if (write) {
        vector<uint8_t> compressed = zlib_compress_bytes(store);
        write_object_file(oid, compressed);
    }

    return oid;
}
