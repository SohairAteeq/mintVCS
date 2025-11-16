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
static const fs::path HEAD_PATH = REPO_PATH / "Head";

// Read the first non-empty line from a file and trim whitespace.
// Returns empty string if file can't be read or is empty.
string getCommitHashFromFile(const fs::path &path) {
    if (!fs::exists(path)) {
        cerr << "getCommitHashFromFile: file not found: " << path.string() << "\n";
        return string();
    }
    ifstream f(path);
    if (!f.is_open()) {
        cerr << "getCommitHashFromFile: cannot open file: " << path.string() << "\n";
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

void createBranch(const string &name) {
    if (!fs::exists(HEAD_PATH)) {
        cerr << "Repository not initialized. Please commit first.\n";
        return;
    }

    // ensure refs/heads directory exists
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

    string headCommitHash = getCommitHashFromFile(HEAD_PATH);
    if (headCommitHash.empty()) {
        cerr << "HEAD does not point to a commit; cannot create branch.\n";
        return;
    }

    ofstream out(newBranchPath);
    if (!out.is_open()) {
        cerr << "Failed to create branch file: " << newBranchPath.string() << "\n";
        return;
    }
    out << headCommitHash << "\n";
    out.close();

    cout << "Created branch '" << name << "' at " << headCommitHash << "\n";
}

void listBranches() {
    if (!fs::exists(HEAD_PATH) || !fs::exists(REFS_HEADS)) {
        cerr << "Repository not initialized or no branches (refs/heads missing).\n";
        return;
    }

    string headCommitHash = getCommitHashFromFile(HEAD_PATH);
    string currentBranch;
    for (const auto &entry : fs::directory_iterator(REFS_HEADS)) {
        if (!entry.is_regular_file()) continue;
        string val = getCommitHashFromFile(entry.path());
        if (!val.empty() && val == headCommitHash) {
            currentBranch = entry.path().filename().string();
            break;
        }
    }

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
        if (b == currentBranch) cout << "* " << b << "\n";
        else cout << "  " << b << "\n";
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

    string headCommitHash = getCommitHashFromFile(HEAD_PATH);
    string currentBranch;
    for (const auto &entry : fs::directory_iterator(REFS_HEADS)) {
        if (!entry.is_regular_file()) continue;
        string val = getCommitHashFromFile(entry.path());
        if (!val.empty() && val == headCommitHash) {
            currentBranch = entry.path().filename().string();
            break;
        }
    }

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

    string headCommitHash = getCommitHashFromFile(HEAD_PATH);
    string currentBranch;
    for (const auto &entry : fs::directory_iterator(REFS_HEADS)) {
        if (!entry.is_regular_file()) continue;
        string val = getCommitHashFromFile(entry.path());
        if (!val.empty() && val == headCommitHash) {
            currentBranch = entry.path().filename().string();
            break;
        }
    }

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
