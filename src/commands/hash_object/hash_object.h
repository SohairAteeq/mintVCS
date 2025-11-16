// hash_object.h
#ifndef HASH_OBJECT_H
#define HASH_OBJECT_H

#include <string>
#include <vector>
#include <cstdint>

std::string hash_object(const std::string &filepath, bool write);
std::vector<uint8_t> read_file_bytes(const std::string &path);
std::vector<uint8_t> read_object_file(const std::string &path);
std::vector<uint8_t> zlib_compress_bytes(const std::vector<uint8_t> &in);
std::vector<uint8_t> zlib_decompress_bytes(const std::vector<uint8_t> &in);
void write_object_file(const std::string &oid_hex, const std::vector<uint8_t> &compressed);
std::string sha1_hex_of_bytes(const std::vector<uint8_t> &data);

#endif