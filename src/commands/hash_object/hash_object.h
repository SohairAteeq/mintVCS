#ifndef HASH_OBJECT_H
#define HASH_OBJECT_H

#include <string>
#include <cstdint>
using namespace std;

string hash_object(const string &filepath, bool write);
string sha1_hex_of_bytes(const vector<uint8_t> &data);
vector<uint8_t> zlib_compress_bytes(const vector<uint8_t> &in);
void write_object_file(const string &oid_hex, const vector<uint8_t> &compressed);


#endif