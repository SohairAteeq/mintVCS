#ifndef BRANCH_H
#define BRANCH_H

#include <string>
using namespace std;
#include <filesystem>
namespace fs = std::filesystem;

string getCommitHashFromFile(const fs::path &path);
void createBranch(const string &name);
void listBranches();
void deleteBranch(const string &name);
void renameBranch(const string &oldName, const string &newName);

#endif