#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <sstream>
#include "../hash_object/hash_object.h"

namespace fs = std::filesystem;
using namespace std;

unordered_set<string> readIgnoreList(const string &ignoreFile) {
    unordered_set<string> ignores;
    ifstream file(ignoreFile);
    string line;

    if (!file.is_open()) {
        return ignores;
    }

    while (getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;
        replace(line.begin(), line.end(), '\\', '/');

        if (!line.empty() && line.back() == '/')
            line.pop_back();

        ignores.insert(line);
    }

    return ignores;
}

bool isIgnored(const fs::path &relativePath, const unordered_set<string> &ignores) {
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

string getFileMode(const fs::path &path) {
    return "100644";
}

struct IndexEntry {
    string mode;
    string type;
    string oid;
    string path;
};

unordered_map<string, IndexEntry> readIndex(const string &indexPath) {
    unordered_map<string, IndexEntry> entries;
    ifstream file(indexPath);

    if (!file.is_open()) {
        return entries;
    }

    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;

        istringstream iss(line);
        IndexEntry entry;

        if (iss >> entry.mode >> entry.type >> entry.oid) {
            getline(iss, entry.path);
            entry.path.erase(0, entry.path.find_first_not_of(" \t"));

            entries[entry.path] = entry;
        }
    }

    return entries;
}

void writeIndex(const string &indexPath, const unordered_map<string, IndexEntry> &entries) {
    vector<IndexEntry> sorted;
    for (const auto &pair : entries) {
        sorted.push_back(pair.second);
    }
    sort(sorted.begin(), sorted.end(),
         [](const IndexEntry &a, const IndexEntry &b) { return a.path < b.path; });

    fs::create_directories(".mintvcs");

    ofstream file(indexPath);
    if (!file.is_open()) {
        throw runtime_error("Unable to write index file: " + indexPath);
    }

    for (const auto &entry : sorted) {
        file << entry.mode << " " << entry.type << " " << entry.oid << " " << entry.path << "\n";
    }
}

void addFile(const fs::path &filepath,
             unordered_map<string, IndexEntry> &indexEntries,
             const unordered_set<string> &ignores) {

    fs::path relativePath = fs::relative(filepath);
    string relStr = relativePath.generic_string();

    if (isIgnored(relativePath, ignores)) {
        cerr << "Skipping ignored file: " << relStr << endl;
        return;
    }

    if (!fs::exists(filepath) || !fs::is_regular_file(filepath)) {
        cerr << "Not a valid file: " << relStr << endl;
        return;
    }

    try {
        string oid = hash_object(relStr, true);

        IndexEntry entry;
        entry.mode = getFileMode(filepath);
        entry.type = "blob";
        entry.oid = oid;
        entry.path = relStr;

        indexEntries[relStr] = entry;

        cout << "Added: " << relStr << " (OID: " << oid << ")" << endl;

    } catch (const exception &e) {
        cerr << "Error adding file " << relStr << ": " << e.what() << endl;
    }
}

void addDirectory(const fs::path &dir,
                  unordered_map<string, IndexEntry> &indexEntries,
                  const unordered_set<string> &ignores,
                  const fs::path &root) {

    for (auto &entry : fs::directory_iterator(dir)) {
        fs::path relativePath = fs::relative(entry.path(), root);

        if (isIgnored(relativePath, ignores)) {
            continue;
        }

        if (entry.is_directory()) {
            addDirectory(entry.path(), indexEntries, ignores, root);
        } else if (entry.is_regular_file()) {
            addFile(entry.path(), indexEntries, ignores);
        }
    }
}

void add(const vector<string> &paths) {
    string indexPath = ".mintvcs/index";

    if (!fs::exists(".mintvcs")) {
        cerr << "Error: Not a mintvcs repository. Run 'mintvcs init' first." << endl;
        return;
    }

    unordered_set<string> ignores = readIgnoreList(".mintvcsignore");

    unordered_map<string, IndexEntry> indexEntries = readIndex(indexPath);

    fs::path root = fs::current_path();

    for (const string &pathStr : paths) {
        fs::path path(pathStr);

        if (pathStr == ".") {
            cout << "Adding all files..." << endl;
            addDirectory(root, indexEntries, ignores, root);
        } else if (fs::is_directory(path)) {
            cout << "Adding directory: " << pathStr << endl;
            addDirectory(path, indexEntries, ignores, root);
        } else if (fs::exists(path)) {
            addFile(path, indexEntries, ignores);
        } else {
            cerr << "Path does not exist: " << pathStr << endl;
        }
    }

    try {
        writeIndex(indexPath, indexEntries);
        cout << "\nIndex updated successfully. " << indexEntries.size() << " files staged." << endl;
    } catch (const exception &e) {
        cerr << "Error writing index: " << e.what() << endl;
    }
}