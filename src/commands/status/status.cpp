#include "status.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <algorithm>

#include "../hash_object/hash_object.h"

using namespace std;
namespace fs = std::filesystem;

static string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static string getCurrentBranch() {
    fs::path headPath = ".mintvcs/HEAD";
    if (!fs::exists(headPath)) {
        return "";
    }
    
    ifstream headFile(headPath);
    if (!headFile) {
        return "";
    }

    string line;
    getline(headFile, line);
    headFile.close();
    line = trim(line);
    
    if (line.rfind("ref:", 0) == 0 || line.rfind("ref: ", 0) == 0) {
        size_t colonPos = line.find(':');
        string refPath = trim(line.substr(colonPos + 1));
        
        if (refPath.rfind("refs/heads/", 0) == 0) {
            return refPath.substr(11);
        }
    }
    
    return "(detached HEAD)";
}

static string resolveHeadToCommit() {
    fs::path headPath = ".mintvcs/HEAD";
    if (!fs::exists(headPath)) {
        return "";
    }
    
    ifstream headFile(headPath);
    if (!headFile) {
        return "";
    }

    string line;
    getline(headFile, line);
    headFile.close();
    line = trim(line);
    
    if (line.empty()) {
        return "";
    }

    if (line.rfind("ref:", 0) == 0 || line.rfind("ref: ", 0) == 0) {
        size_t colonPos = line.find(':');
        string refPath = trim(line.substr(colonPos + 1));
        
        fs::path refFilePath = fs::path(".mintvcs") / refPath;
        if (!fs::exists(refFilePath)) {
            return "";
        }
        
        ifstream refFile(refFilePath);
        if (!refFile) {
            return "";
        }
        
        string commitHash;
        getline(refFile, commitHash);
        refFile.close();
        
        return trim(commitHash);
    }
    
    return line;
}

static string readObject(const string &oid) {
    fs::path objPath = fs::path(".mintvcs/objects") / oid.substr(0, 2) / oid.substr(2);
    if (!fs::exists(objPath)) {
        throw runtime_error("Object not found: " + oid);
    }
    vector<uint8_t> data = read_object_file(objPath.string());
    vector<uint8_t> decompressed = zlib_decompress_bytes(data);
    return string(decompressed.begin(), decompressed.end());
}

static void parseObject(const string &raw, string &type, string &content) {
    size_t nullPos = raw.find('\0');
    if (nullPos == string::npos) {
        throw runtime_error("Invalid object format");
    }
    
    string header = raw.substr(0, nullPos);
    content = raw.substr(nullPos + 1);
    
    size_t spacePos = header.find(' ');
    if (spacePos == string::npos) {
        throw runtime_error("Invalid object header");
    }
    
    type = header.substr(0, spacePos);
}

static string getTreeFromCommit(const string &commitOid) {
    string raw = readObject(commitOid);
    string type, content;
    parseObject(raw, type, content);
    
    if (type != "commit") {
        throw runtime_error("Object is not a commit");
    }
    
    istringstream ss(content);
    string line;
    while (getline(ss, line)) {
        if (line.rfind("tree ", 0) == 0) {
            return trim(line.substr(5));
        }
    }
    
    throw runtime_error("No tree found in commit");
}

struct TreeEntry {
    string mode;
    string name;
    string oid;
    bool isDir;
};

static vector<TreeEntry> parseTree(const string &treeOid) {
    string raw = readObject(treeOid);
    string type, content;
    parseObject(raw, type, content);
    
    if (type != "tree") {
        throw runtime_error("Object is not a tree");
    }
    
    vector<TreeEntry> entries;
    size_t pos = 0;
    
    while (pos < content.size()) {
        size_t spacePos = content.find(' ', pos);
        if (spacePos == string::npos) break;
        string mode = content.substr(pos, spacePos - pos);
        pos = spacePos + 1;
        
        size_t nullPos = content.find('\0', pos);
        if (nullPos == string::npos) break;
        string name = content.substr(pos, nullPos - pos);
        pos = nullPos + 1;
        
        if (pos + 40 > content.size()) break;
        string oid = content.substr(pos, 40);
        pos += 40;
        
        TreeEntry entry;
        entry.mode = mode;
        entry.name = name;
        entry.oid = oid;
        entry.isDir = (mode == "40000");
        
        entries.push_back(entry);
    }
    
    return entries;
}

static void collectCommitFiles(const string &treeOid, 
                               unordered_map<string, string> &files,
                               const string &prefix = "") {
    auto entries = parseTree(treeOid);
    
    for (const auto &entry : entries) {
        string path = prefix.empty() ? entry.name : prefix + "/" + entry.name;
        
        if (entry.isDir) {
            collectCommitFiles(entry.oid, files, path);
        } else {
            files[path] = entry.oid;
        }
    }
}

struct IndexEntry {
    string mode;
    string type;
    string oid;
    string path;
};

static unordered_map<string, IndexEntry> readIndex() {
    unordered_map<string, IndexEntry> entries;
    
    if (!fs::exists(".mintvcs/index")) {
        return entries;
    }
    
    ifstream indexFile(".mintvcs/index");
    string line;
    
    while (getline(indexFile, line)) {
        if (line.empty()) continue;
        
        istringstream ss(line);
        IndexEntry entry;
        
        if (ss >> entry.mode >> entry.type >> entry.oid) {
            getline(ss, entry.path);
            entry.path = trim(entry.path);
            if (!entry.path.empty()) {
                entries[entry.path] = entry;
            }
        }
    }
    
    return entries;
}

static unordered_set<string> readIgnoreList() {
    unordered_set<string> ignores;
    ignores.insert(".mintvcs");
    ignores.insert(".mintvcsignore");
    
    ifstream file(".mintvcsignore");
    if (!file.is_open()) {
        return ignores;
    }
    
    string line;
    while (getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        replace(line.begin(), line.end(), '\\', '/');
        if (!line.empty() && line.back() == '/') {
            line.pop_back();
        }
        
        ignores.insert(line);
    }
    
    return ignores;
}

static bool isIgnored(const fs::path &relativePath, const unordered_set<string> &ignores) {
    string relStr = relativePath.generic_string();
    
    string firstComponent;
    size_t slashPos = relStr.find('/');
    if (slashPos != string::npos) {
        firstComponent = relStr.substr(0, slashPos);
    } else {
        firstComponent = relStr;
    }
    
    if (ignores.count(firstComponent)) {
        return true;
    }
    if (ignores.count(relStr)) {
        return true;
    }
    
    return false;
}

static void collectWorkingFiles(const fs::path &dir, 
                                unordered_set<string> &files,
                                const unordered_set<string> &ignores,
                                const fs::path &root) {
    for (const auto &entry : fs::directory_iterator(dir)) {
        fs::path relativePath = fs::relative(entry.path(), root);
        
        if (isIgnored(relativePath, ignores)) {
            continue;
        }
        
        if (entry.is_directory()) {
            collectWorkingFiles(entry.path(), files, ignores, root);
        } else if (entry.is_regular_file()) {
            files.insert(relativePath.generic_string());
        }
    }
}

void mintvcs_status() {
    if (!fs::exists(".mintvcs")) {
        cerr << "Not a mintvcs repository\n";
        return;
    }
    
    string branch = getCurrentBranch();
    cout << "On branch " << branch << "\n\n";
    
    string commitOid = resolveHeadToCommit();
    unordered_map<string, string> commitFiles;
    
    if (!commitOid.empty()) {
        try {
            string treeOid = getTreeFromCommit(commitOid);
            collectCommitFiles(treeOid, commitFiles);
        } catch (...) {
        }
    }
    
    auto index = readIndex();
    auto ignores = readIgnoreList();
    
    unordered_set<string> workingFiles;
    collectWorkingFiles(fs::current_path(), workingFiles, ignores, fs::current_path());
    
    vector<string> staged;
    vector<string> modified;
    vector<string> deleted;
    vector<string> untracked;
    
    for (const auto &pair : index) {
        const string &path = pair.first;
        const IndexEntry &entry = pair.second;
        
        bool inCommit = commitFiles.find(path) != commitFiles.end();
        bool changedFromCommit = !inCommit || commitFiles[path] != entry.oid;
        
        if (workingFiles.count(path)) {
            try {
                string currentOid = hash_object(path, false);
                if (currentOid != entry.oid) {
                    modified.push_back(path);
                } else if (changedFromCommit) {
                    staged.push_back(path);
                }
            } catch (...) {
                modified.push_back(path);
            }
        } else {
            if (changedFromCommit) {
                staged.push_back(path);
            }
            deleted.push_back(path);
        }
    }
    
    for (const string &path : workingFiles) {
        if (index.find(path) == index.end()) {
            untracked.push_back(path);
        }
    }
    
    sort(staged.begin(), staged.end());
    sort(modified.begin(), modified.end());
    sort(deleted.begin(), deleted.end());
    sort(untracked.begin(), untracked.end());
    
    if (!staged.empty()) {
        cout << "Changes to be committed:\n";
        cout << "  (use \"mintvcs reset <file>...\" to unstage)\n\n";
        for (const auto &file : staged) {
            bool inCommit = commitFiles.find(file) != commitFiles.end();
            if (inCommit) {
                cout << "\tmodified:   " << file << "\n";
            } else {
                cout << "\tnew file:   " << file << "\n";
            }
        }
        cout << "\n";
    }
    
    if (!modified.empty()) {
        cout << "Changes not staged for commit:\n";
        cout << "  (use \"mintvcs add <file>...\" to update what will be committed)\n\n";
        for (const auto &file : modified) {
            cout << "\tmodified:   " << file << "\n";
        }
        cout << "\n";
    }
    
    if (!deleted.empty() && !modified.empty()) {
        for (const auto &file : deleted) {
            if (find(modified.begin(), modified.end(), file) == modified.end()) {
                if (staged.empty() && modified.empty()) {
                    cout << "Changes not staged for commit:\n";
                    cout << "  (use \"mintvcs add <file>...\" to update what will be committed)\n\n";
                }
                cout << "\tdeleted:    " << file << "\n";
            }
        }
        cout << "\n";
    }
    
    if (!untracked.empty()) {
        cout << "Untracked files:\n";
        cout << "  (use \"mintvcs add <file>...\" to include in what will be committed)\n\n";
        for (const auto &file : untracked) {
            cout << "\t" << file << "\n";
        }
        cout << "\n";
    }
    
    if (staged.empty() && modified.empty() && deleted.empty() && untracked.empty()) {
        cout << "nothing to commit, working tree clean\n";
    }
}