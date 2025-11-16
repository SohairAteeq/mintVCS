// commit.cpp  (replace your existing file with this)
// Safer, fixed version for MintVCS commit command.

#include <iostream>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <memory>

#include "../hash_object/hash_object.h"
#include "commit.h"

using namespace std;
namespace fs = std::filesystem;

struct IndexEntry {
    string mode;
    string type;
    string sha1;
    string path;
};

static string resolveHeadToCommit() {
    ifstream headFile(".mintvcs/HEAD");
    if (!headFile) {
        return "";
    }

    string line;
    getline(headFile, line);
    headFile.close();

    line.erase(0, line.find_first_not_of(" \t\r\n"));
    line.erase(line.find_last_not_of(" \t\r\n") + 1);
    
    if (line.empty()) {
        return "";
    }

    // Check if HEAD contains a symbolic reference
    if (line.rfind("ref:", 0) == 0 || line.rfind("ref: ", 0) == 0) {
        size_t colonPos = line.find(':');
        string refPath = line.substr(colonPos + 1);
        
        // Trim whitespace from refPath
        refPath.erase(0, refPath.find_first_not_of(" \t\r\n"));
        refPath.erase(refPath.find_last_not_of(" \t\r\n") + 1);
        
        fs::path refFilePath = fs::path(".mintvcs") / refPath;
        if (!fs::exists(refFilePath)) {
            return ""; // Reference doesn't exist yet (first commit)
        }
        
        ifstream refFile(refFilePath);
        if (!refFile) {
            return ""; // Can't read reference
        }
        
        string commitHash;
        getline(refFile, commitHash);
        refFile.close();
        
        // Trim whitespace from commitHash
        commitHash.erase(0, commitHash.find_first_not_of(" \t\r\n"));
        commitHash.erase(commitHash.find_last_not_of(" \t\r\n") + 1);
        
        return commitHash;
    }
    
    // HEAD contains direct commit hash
    return line;
}

// Read index lines robustly: read first three tokens and treat remainder as path
vector<IndexEntry> read_index(const string &path) {
    ifstream f(path);
    if (!f) {
        cerr << "Cannot open index file: " << path << endl;
        return {};
    }

    vector<IndexEntry> entries;
    string line;
    while (getline(f, line)) {
        if (line.empty()) continue;
        istringstream ss(line);
        IndexEntry e;
        if (!(ss >> e.mode >> e.type >> e.sha1)) {
            // malformed line, skip with warning
            cerr << "Warning: malformed index line (ignored): " << line << endl;
            continue;
        }
        string rest;
        getline(ss, rest);
        // trim leading spaces
        size_t pos = rest.find_first_not_of(" \t");
        if (pos == string::npos) {
            cerr << "Warning: index entry missing path (ignored): " << line << endl;
            continue;
        }
        e.path = rest.substr(pos);
        entries.push_back(move(e));
    }
    return entries;
}

// Tree node (heap allocated)
struct TreeNode {
    string sha1;              
    string name;        
    bool isDir;
    vector<TreeNode*> children;

    TreeNode(const string &sha, const string &n, bool dir) : sha1(sha), name(n), isDir(dir) {}
};

// free tree nodes recursively
void free_tree(TreeNode* node) {
    if (!node) return;
    for (auto ptr : node->children) free_tree(ptr);
    delete node;
}

// split path by '/'
vector<string> splitPath(const string &path) {
    vector<string> parts;
    string tmp;
    stringstream ss(path);
    while (getline(ss, tmp, '/')) {
        if (!tmp.empty()) parts.push_back(tmp);
    }
    return parts;
}

// build tree: returns pointer to root (heap allocated). Caller responsible to free_tree(root).
TreeNode* build_tree(const vector<IndexEntry> &entries) {
    TreeNode* root = new TreeNode("", "", true);
    for (const auto &e : entries) {
        vector<string> parts = splitPath(e.path);
        TreeNode* current = root;
        for (size_t i = 0; i < parts.size(); ++i) {
            const string &part = parts[i];
            bool isLeaf = (i == parts.size() - 1);

            // find existing child
            TreeNode* next = nullptr;
            for (auto child : current->children) {
                if (child->name == part) {
                    next = child;
                    break;
                }
            }

            if (!next) {
                // create new node
                next = new TreeNode("", part, !isLeaf);
                current->children.push_back(next);
            }

            current = next;
            if (isLeaf) {
                current->sha1 = e.sha1;
            }
        }
    }
    return root;
}

// compute sha1 hex for a string (treating it as bytes)
static string sha1_hex_from_string(const string &s) {
    vector<uint8_t> v(s.begin(), s.end());
    return sha1_hex_of_bytes(v);
}

// store object given oid hex and full content (header+body) -> compress & write
static void storeObjectFull(const string &oid_hex, const string &full_content) {
    vector<uint8_t> raw(full_content.begin(), full_content.end());
    auto compressed = zlib_compress_bytes(raw);
    write_object_file(oid_hex, compressed);
}

string computeTreeHash(TreeNode* node) {
    if (!node) return "";

    if (!node->isDir) {
        return node->sha1;
    }

    string content;
    for (auto child : node->children) {
        string childHash = computeTreeHash(child);
        child->sha1 = childHash;

        string mode = child->isDir ? "40000" : "100644";
        content += mode + " " + child->name + '\0' + childHash;
    }

    string header = "tree " + to_string(content.size()) + '\0';
    string full = header + content;
    string treeHash = sha1_hex_from_string(full);

    storeObjectFull(treeHash, full);
    return treeHash;
}

string createCommitObject(const string &treeHash, const string &parentHash, const string &message) {
    ostringstream body;
    body << "tree " << treeHash << "\n";
    if (!parentHash.empty()) body << "parent " << parentHash << "\n";

    time_t now = time(nullptr);
    body << "author MintVCS <mint@example.com> " << now << " +0000\n";
    body << "committer MintVCS <mint@example.com> " << now << " +0000\n\n";

    body << message << "\n";

    string commitContent = body.str();
    string header = "commit " + to_string(commitContent.size()) + '\0';
    string full_content = header + commitContent;

    string commit_hash = sha1_hex_from_string(full_content);
    storeObjectFull(commit_hash, full_content);

    return commit_hash;
}

void updateHead(const string &hash) {
    fs::create_directories(".mintvcs");
    
    string headContent;
    ifstream headRead(".mintvcs/HEAD");
    if (headRead) {
        getline(headRead, headContent);
        headRead.close();
    }
    
    headContent.erase(0, headContent.find_first_not_of(" \t\r\n"));
    headContent.erase(headContent.find_last_not_of(" \t\r\n") + 1);
    
    if (headContent.rfind("ref:", 0) == 0 || headContent.rfind("ref: ", 0) == 0) {
        size_t colonPos = headContent.find(':');
        string refPath = headContent.substr(colonPos + 1);
        
        refPath.erase(0, refPath.find_first_not_of(" \t\r\n"));
        refPath.erase(refPath.find_last_not_of(" \t\r\n") + 1);
        
        fs::path refFilePath = fs::path(".mintvcs") / refPath;
        fs::create_directories(refFilePath.parent_path());
        
        ofstream refFile(refFilePath, ios::trunc);
        if (refFile) {
            refFile << hash << "\n";
            refFile.close();
        } else {
            cerr << "Warning: unable to write to " << refPath << "\n";
        }
    } else {
        ofstream head(".mintvcs/HEAD", ios::trunc);
        if (!head) {
            cerr << "Warning: unable to write HEAD\n";
            return;
        }
        head << hash << "\n";
        head.close();
    }
}

int mintvcs_commit(const string &message) {
    try {
        fs::path index_path = ".mintvcs/index";
        if (!fs::exists(index_path)) {
            cerr << "Index not found: " << index_path << "\n";
            return 1;
        }

        auto entries = read_index(index_path.string());
        if (entries.empty()) {
            cerr << "Index empty. Nothing to commit.\n";
            return 1;
        }
        sort(entries.begin(), entries.end(), [](const IndexEntry &a, const IndexEntry &b){
            return a.path < b.path;
        });

        TreeNode* root = build_tree(entries);
        if (!root) {
            cerr << "Failed to build tree\n";
            return 1;
        }

        string root_tree_oid = computeTreeHash(root);

        string parent = resolveHeadToCommit();

        string commit_oid = createCommitObject(root_tree_oid, parent, message);

        updateHead(commit_oid);

        cout << "Created commit " << commit_oid << "\n";

        free_tree(root);

        return 0;
    } catch (const exception &ex) {
        cerr << "commit failed: " << ex.what() << "\n";
        return 1;
    }
}

void commit(const string& message) {
    mintvcs_commit(message);
}