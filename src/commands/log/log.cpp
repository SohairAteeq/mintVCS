#include "log.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <vector>

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
    if (!fs::exists(objPath)) throw runtime_error("Object not found: " + objPath.string());
    vector<uint8_t> data = read_object_file(objPath.string());
    vector<uint8_t> decompressed = zlib_decompress_bytes(data);
    return string(decompressed.begin(), decompressed.end());
}

static void parseCommit(const string &content, string &parentHash, string &message) {
    parentHash.clear();
    message.clear();
    istringstream ss(content);
    string line;
    bool messageStart = false;
    while (getline(ss, line)) {
        if (!messageStart) {
            if (line.rfind("parent ", 0) == 0) {
                parentHash = trim(line.substr(7));
            }
            else if (line.empty()) {
                messageStart = true;
            }
        } else {
            if (!message.empty()) message += "\n";
            message += line;
        }
    }
}

static string resolveHead() {
    ifstream headFile(".mintvcs/HEAD");
    if (!headFile) {
        throw runtime_error("No HEAD found");
    }

    string line;
    getline(headFile, line);
    headFile.close();

    line = trim(line);
    
    if (line.empty()) {
        throw runtime_error("HEAD is empty");
    }

    // Check if HEAD contains a symbolic reference (e.g., "ref: refs/heads/main")
    if (line.rfind("ref:", 0) == 0 || line.rfind("ref: ", 0) == 0) {
        size_t colonPos = line.find(':');
        string refPath = trim(line.substr(colonPos + 1));
        
        // Read the reference file
        fs::path refFilePath = fs::path(".mintvcs") / refPath;
        if (!fs::exists(refFilePath)) {
            throw runtime_error("Reference not found: " + refPath + " (no commits yet?)");
        }
        
        ifstream refFile(refFilePath);
        if (!refFile) {
            throw runtime_error("Cannot read reference: " + refPath);
        }
        
        string commitHash;
        getline(refFile, commitHash);
        refFile.close();
        
        commitHash = trim(commitHash);
        
        if (commitHash.empty()) {
            throw runtime_error("Reference is empty: " + refPath);
        }
        
        return commitHash;
    }
    
    // HEAD contains direct commit hash
    return line;
}

void mintvcs_log() {
    try {
        string commitHash = resolveHead();
        
        while (!commitHash.empty()) {
            string content;
            try {
                content = readObject(commitHash);
            } catch (const exception &ex) {
                cerr << "Error reading commit " << commitHash << ": " << ex.what() << "\n";
                return;
            }

            string parentHash, message;
            parseCommit(content, parentHash, message);

            cout << "commit " << commitHash << "\n";
            if (!parentHash.empty()) {
                cout << "parent " << parentHash << "\n";
            }
            cout << "\n    " << message << "\n\n";

            commitHash = parentHash;
        }
    } catch (const exception &ex) {
        cerr << "Error: " << ex.what() << "\n";
    }
}