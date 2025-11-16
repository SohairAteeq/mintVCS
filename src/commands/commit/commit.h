#ifndef COMMIT_H
#define COMMIT_H

#include <string>
#include <vector>
using namespace std;

struct TreeNode {
    string sha1;
    string name;
    bool isDir;
    vector<TreeNode*> children;

    TreeNode(): sha1(""), name(""), isDir(false) {}
    TreeNode(const string &sha, const string &n, bool dir): sha1(sha), name(n), isDir(dir) {}
};


void commit(const string& message);
static void storeObjectFull(const string &oid_hex, const string &full_content);
string computeTreeHash(class TreeNode* node);
string createCommitObject(const string &treeHash, vector<string> &parentHash, const string &message);
void updateHead(const string &hash, const string &branch);

#endif