#ifndef ADD_H
#define ADD_H

#include <string>
#include <vector>

// Main add function - stages files for commit
// Accepts file paths, directory paths, or "." to add all files
void add(const std::vector<std::string> &paths);

#endif