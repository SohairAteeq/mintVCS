#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>

using namespace std;
namespace fs = std::filesystem;

static const fs::path REPO_PATH = ".mintvcs";
static const fs::path REFS_HEADS = REPO_PATH / "refs" / "heads";
static const fs::path HEAD_PATH = REPO_PATH / "HEAD";

static string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string getCommitHashFromFile(const fs::path &path) {
    if (!fs::exists(path)) {
        return string();
    }
    ifstream f(path);
    if (!f.is_open()) {
        return string();
    }
    string line;
    while (getline(f, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        string trimmed = line.substr(start, end - start + 1);
        return trimmed;
    }
    return string();
}

static string resolveHeadToCommit() {
    if (!fs::exists(HEAD_PATH)) {
        return "";
    }
    
    ifstream headFile(HEAD_PATH);
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
        
        fs::path refFilePath = REPO_PATH / refPath;
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

static string getCurrentBranch() {
    if (!fs::exists(HEAD_PATH)) {
        return "";
    }
    
    ifstream headFile(HEAD_PATH);
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
    
    return "";
}

void createBranch(const string &name) {
    if (!fs::exists(HEAD_PATH)) {
        cerr << "Repository not initialized. Please commit first.\n";
        return;
    }

    error_code ec;
    fs::create_directories(REFS_HEADS, ec);
    if (ec) {
        cerr << "Failed to create refs/heads directory: " << ec.message() << "\n";
        return;
    }

    fs::path newBranchPath = REFS_HEADS / name;
    if (fs::exists(newBranchPath)) {
        cerr << "Branch '" << name << "' already exists.\n";
        return;
    }

    string commitHash = resolveHeadToCommit();
    if (commitHash.empty()) {
        cerr << "HEAD does not point to a commit; cannot create branch.\n";
        return;
    }

    ofstream out(newBranchPath);
    if (!out.is_open()) {
        cerr << "Failed to create branch file: " << newBranchPath.string() << "\n";
        return;
    }
    out << commitHash << "\n";
    out.close();

    cout << "Created branch '" << name << "' at " << commitHash << "\n";
}

void listBranches() {
    if (!fs::exists(HEAD_PATH) || !fs::exists(REFS_HEADS)) {
        cerr << "Repository not initialized or no branches (refs/heads missing).\n";
        return;
    }

    string currentBranch = getCurrentBranch();

    vector<string> branches;
    for (const auto &entry : fs::directory_iterator(REFS_HEADS)) {
        if (!entry.is_regular_file()) continue;
        branches.push_back(entry.path().filename().string());
    }
    sort(branches.begin(), branches.end());

    if (branches.empty()) {
        cout << "(no branches)\n";
        return;
    }

    for (const auto &b : branches) {
        if (b == currentBranch) {
            cout << "* " << b << "\n";
        } else {
            cout << "  " << b << "\n";
        }
    }
}

void deleteBranch(const string &name) {
    if (!fs::exists(HEAD_PATH)) {
        cerr << "Repository not initialized. Please commit first.\n";
        return;
    }
    if (!fs::exists(REFS_HEADS)) {
        cerr << "No branches to delete.\n";
        return;
    }

    string currentBranch = getCurrentBranch();

    if (name == currentBranch) {
        cerr << "Cannot delete the current checked out branch: " << name << "\n";
        return;
    }

    fs::path branchPath = REFS_HEADS / name;
    if (!fs::exists(branchPath)) {
        cerr << "Branch '" << name << "' does not exist.\n";
        return;
    }

    error_code ec;
    fs::remove(branchPath, ec);
    if (ec) {
        cerr << "Failed to delete branch '" << name << "': " << ec.message() << "\n";
        return;
    }

    cout << "Deleted branch '" << name << "'\n";
}

void renameBranch(const string &oldName, const string &newName) {
    if (!fs::exists(HEAD_PATH)) {
        cerr << "Repository not initialized. Please commit first.\n";
        return;
    }
    if (!fs::exists(REFS_HEADS)) {
        cerr << "No branches to rename.\n";
        return;
    }

    string currentBranch = getCurrentBranch();

    if (oldName == currentBranch) {
        cerr << "Cannot rename the current checked out branch: " << oldName << "\n";
        return;
    }

    fs::path oldBranchPath = REFS_HEADS / oldName;
    fs::path newBranchPath = REFS_HEADS / newName;
    if (!fs::exists(oldBranchPath)) {
        cerr << "Branch '" << oldName << "' does not exist.\n";
        return;
    }
    if (fs::exists(newBranchPath)) {
        cerr << "Branch '" << newName << "' already exists.\n";
        return;
    }

    error_code ec;
    fs::rename(oldBranchPath, newBranchPath, ec);
    if (ec) {
        cerr << "Failed to rename branch: " << ec.message() << "\n";
        return;
    }

    cout << "Renamed branch '" << oldName << "' to '" << newName << "'\n";
}