#ifndef BRANCH_H
#define BRANCH_H

#include <string>
#include <filesystem>

void createBranch(const std::string &name);
void listBranches();
void deleteBranch(const std::string &name);
void renameBranch(const std::string &oldName, const std::string &newName);

std::string getCommitHashFromFile(const std::filesystem::path &path);

#endif