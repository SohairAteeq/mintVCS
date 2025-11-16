#include "checkout.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <vector>
#include <unordered_set>

#include "../hash_object/hash_object.h"

using namespace std;
namespace fs = std::filesystem;

static string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
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
        throw runtime_error("Object is not a commit: " + commitOid);
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
        throw runtime_error("Object is not a tree: " + treeOid);
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

static void checkoutTree(const string &treeOid, const fs::path &targetPath) {
    auto entries = parseTree(treeOid);
    
    for (const auto &entry : entries) {
        fs::path entryPath = targetPath / entry.name;
        
        if (entry.isDir) {
            fs::create_directories(entryPath);
            checkoutTree(entry.oid, entryPath);
        } else {
            string raw = readObject(entry.oid);
            string type, content;
            parseObject(raw, type, content);
            
            if (type != "blob") {
                cerr << "Warning: expected blob but got " << type << " for " << entryPath << endl;
                continue;
            }
            
            ofstream outFile(entryPath, ios::binary);
            if (!outFile) {
                cerr << "Warning: cannot write file " << entryPath << endl;
                continue;
            }
            outFile.write(content.data(), content.size());
            outFile.close();
            
            cout << "Checked out: " << entryPath.generic_string() << endl;
        }
    }
}

static unordered_set<string> getTrackedFiles() {
    unordered_set<string> tracked;
    
    if (!fs::exists(".mintvcs/index")) {
        return tracked;
    }
    
    ifstream indexFile(".mintvcs/index");
    string line;
    
    while (getline(indexFile, line)) {
        if (line.empty()) continue;
        
        istringstream ss(line);
        string mode, type, oid, path;
        
        if (ss >> mode >> type >> oid) {
            getline(ss, path);
            path = trim(path);
            if (!path.empty()) {
                tracked.insert(path);
            }
        }
    }
    
    return tracked;
}

static void removeUntrackedFiles(const unordered_set<string> &newTracked) {
    auto oldTracked = getTrackedFiles();
    
    for (const auto &oldFile : oldTracked) {
        if (newTracked.find(oldFile) == newTracked.end()) {
            fs::path filePath(oldFile);
            if (fs::exists(filePath)) {
                try {
                    fs::remove(filePath);
                    cout << "Removed: " << oldFile << endl;
                } catch (const exception &e) {
                    cerr << "Warning: could not remove " << oldFile << ": " << e.what() << endl;
                }
            }
        }
    }
}

static void updateIndex(const string &treeOid, const fs::path &prefix = "") {
    auto entries = parseTree(treeOid);
    
    static ofstream *indexFile = nullptr;
    static bool firstCall = true;
    
    if (firstCall) {
        indexFile = new ofstream(".mintvcs/index", ios::trunc);
        if (!*indexFile) {
            throw runtime_error("Cannot write to index file");
        }
        firstCall = false;
    }
    
    for (const auto &entry : entries) {
        fs::path entryPath = prefix / entry.name;
        
        if (entry.isDir) {
            updateIndex(entry.oid, entryPath);
        } else {
            *indexFile << entry.mode << " blob " << entry.oid << " " << entryPath.generic_string() << "\n";
        }
    }
    
    if (prefix.empty()) {
        indexFile->close();
        delete indexFile;
        indexFile = nullptr;
        firstCall = true;
    }
}

static string resolveReference(const string &ref) {
    string refToResolve = ref;
    
    if (ref == "HEAD") {
        ifstream headFile(".mintvcs/HEAD");
        if (!headFile) {
            throw runtime_error("HEAD not found");
        }
        
        string line;
        getline(headFile, line);
        headFile.close();
        line = trim(line);
        
        if (line.rfind("ref:", 0) == 0 || line.rfind("ref: ", 0) == 0) {
            size_t colonPos = line.find(':');
            refToResolve = trim(line.substr(colonPos + 1));
        } else {
            return line;
        }
    }
    
    if (refToResolve.rfind("refs/", 0) == 0) {
        fs::path refPath = fs::path(".mintvcs") / refToResolve;
        if (fs::exists(refPath)) {
            ifstream refFile(refPath);
            string commitOid;
            getline(refFile, commitOid);
            refFile.close();
            return trim(commitOid);
        }
    }
    
    if (refToResolve.rfind("refs/heads/", 0) != 0) {
        fs::path refPath = fs::path(".mintvcs/refs/heads") / refToResolve;
        if (fs::exists(refPath)) {
            ifstream refFile(refPath);
            string commitOid;
            getline(refFile, commitOid);
            refFile.close();
            return trim(commitOid);
        }
    }
    
    if (refToResolve.size() >= 7 && refToResolve.size() <= 40) {
        fs::path objDir = fs::path(".mintvcs/objects") / refToResolve.substr(0, 2);
        if (fs::exists(objDir)) {
            for (const auto &entry : fs::directory_iterator(objDir)) {
                string fullOid = refToResolve.substr(0, 2) + entry.path().filename().string();
                if (fullOid.rfind(refToResolve, 0) == 0) {
                    return fullOid;
                }
            }
        }
    }
    
    throw runtime_error("Cannot resolve reference: " + ref);
}

static void updateHEADToCommit(const string &commitOid) {
    ofstream headFile(".mintvcs/HEAD", ios::trunc);
    if (!headFile) {
        throw runtime_error("Cannot write to HEAD");
    }
    headFile << commitOid << "\n";
    headFile.close();
}

static void updateHEADToBranch(const string &branchName) {
    ofstream headFile(".mintvcs/HEAD", ios::trunc);
    if (!headFile) {
        throw runtime_error("Cannot write to HEAD");
    }
    headFile << "ref: refs/heads/" << branchName << "\n";
    headFile.close();
}

void mintvcs_checkout(const string &target) {
    try {
        if (!fs::exists(".mintvcs")) {
            cerr << "Not a mintvcs repository\n";
            return;
        }
        
        unordered_set<string> newTrackedFiles;
        string commitOid;
        bool isBranch = false;
        string branchName;
        
        fs::path branchPath = fs::path(".mintvcs/refs/heads") / target;
        if (fs::exists(branchPath)) {
            isBranch = true;
            branchName = target;
            ifstream branchFile(branchPath);
            getline(branchFile, commitOid);
            branchFile.close();
            commitOid = trim(commitOid);
        } else {
            commitOid = resolveReference(target);
        }
        
        if (commitOid.empty()) {
            cerr << "Cannot resolve: " << target << endl;
            return;
        }
        
        string treeOid = getTreeFromCommit(commitOid);
        
        removeUntrackedFiles(newTrackedFiles);
        
        checkoutTree(treeOid, fs::current_path());
        
        updateIndex(treeOid);
        
        if (isBranch) {
            updateHEADToBranch(branchName);
            cout << "Switched to branch '" << branchName << "'\n";
        } else {
            updateHEADToCommit(commitOid);
            cout << "HEAD is now at " << commitOid.substr(0, 7) << "\n";
        }
        
    } catch (const exception &ex) {
        cerr << "checkout failed: " << ex.what() << "\n";
    }
}