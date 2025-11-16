#include <bits/stdc++.h>
#include <filesystem>
#include "../hash_object/hash_object.h"
#include "../commit/commit.h"
#include "../branch/branch.h"

using namespace std;
namespace fs = std::filesystem;

static const fs::path repoPath = ".mintvcs";

// Build path for object file from 40-char hex sha
static fs::path objectPathFromHex(const string &hex40) {
    if (hex40.size() < 2) throw runtime_error("objectPathFromHex: invalid hex length");
    return repoPath / "objects" / hex40.substr(0,2) / hex40.substr(2);
}

// Read compressed object file and return decompressed string (header + body)
static string readObjectDecompressed(const string &hex40) {
    fs::path p = objectPathFromHex(hex40);
    if (!fs::exists(p)) throw runtime_error("Object not found: " + p.string());
    vector<uint8_t> comp = read_file_bytes(p.string()); // from hash_object.h
    vector<uint8_t> raw = zlib_decompress_bytes(comp);
    string decomp(raw.begin(), raw.end());
    return decomp;
}

// Create and write a blob object from raw contents (no header).
// Returns blob hex (40 chars).
static string writeBlobObjectFromString(const string &data) {
    string header = "blob " + to_string(data.size()) + '\0';
    string full = header + data;
    // compute oid
    vector<uint8_t> full_bytes(full.begin(), full.end());
    string oid = sha1_hex_of_bytes(full_bytes); // from hash_object.h
    // compress and write
    vector<uint8_t> compressed = zlib_compress_bytes(full_bytes);
    write_object_file(oid, compressed);
    return oid;
}

// ----------------- Parse functions -----------------

// Parse decompressed commit object (return tree hash and parents)
static pair<string, vector<string>> parseCommitObject(const string &decompressed) {
    // Decompressed looks like: "commit <size>\0" + body lines (e.g. "tree <hash>\nparent <hash>\n...\n\nmsg")
    size_t nul = decompressed.find('\0');
    if (nul == string::npos) throw runtime_error("parseCommitObject: malformed commit (no NUL)");
    string body = decompressed.substr(nul + 1);
    istringstream iss(body);
    string line;
    string treeHash;
    vector<string> parents;
    while (getline(iss, line)) {
        if (line.rfind("tree ", 0) == 0) {
            string t = line.substr(5);
            if (t.size() == 40) treeHash = t;
        } else if (line.rfind("parent ", 0) == 0) {
            string p = line.substr(7);
            if (p.size() == 40) parents.push_back(p);
        } else if (line.empty()) {
            break; // end of headers
        }
    }
    if (treeHash.empty()) throw runtime_error("parseCommitObject: tree hash not found");
    return {treeHash, parents};
}

// Parse decompressed tree object produced by your computeTreeHash
// Format assumed:
// "tree <size>\0" + repeated entries:
//   "<mode> <name>\0<40-hex>" (40 ASCII hex chars) -- note: this matches your computeTreeHash
static TreeNode* parseTreeObjectFromDecompressed(const string &decompressed, const string &treeHex) {
    const char *ptr = decompressed.data();
    const char *end = ptr + decompressed.size();
    const string prefix = "tree ";
    if ((size_t)(end - ptr) < prefix.size() || strncmp(ptr, prefix.c_str(), prefix.size()) != 0)
        throw runtime_error("parseTreeObject: invalid header");

    // skip "tree " and decimal size upto NUL
    ptr += prefix.size();
    while (ptr < end && *ptr != '\0') ++ptr;
    if (ptr >= end) throw runtime_error("parseTreeObject: malformed header (no NUL)");
    ++ptr; // skip NUL

    TreeNode *dir = new TreeNode(treeHex, "", true);
    dir->children.clear();

    while (ptr < end) {
        // mode (until space)
        string mode;
        while (ptr < end && *ptr != ' ') mode.push_back(*ptr++);
        if (ptr >= end || *ptr != ' ') throw runtime_error("parseTreeObject: malformed entry (mode)");
        ++ptr; // skip space

        bool isDir = (mode == "40000");

        // name (until NUL)
        string name;
        while (ptr < end && *ptr != '\0') name.push_back(*ptr++);
        if (ptr >= end) throw runtime_error("parseTreeObject: malformed entry (name)");
        ++ptr; // skip NUL

        // next 40 chars are hex sha
        if ((size_t)(end - ptr) < 40) throw runtime_error("parseTreeObject: truncated sha in tree entry");
        string sha = string(ptr, ptr + 40);
        ptr += 40;

        TreeNode *child = new TreeNode(sha, name, isDir);
        dir->children.push_back(child);
    }

    return dir;
}

// Recursively load tree (given tree hex) into TreeNode structure, using parseTreeObjectFromDecompressed
static TreeNode* loadTreeRecursive(const string &treeHex) {
    string decompressed = readObjectDecompressed(treeHex);
    TreeNode *node = parseTreeObjectFromDecompressed(decompressed, treeHex);
    // recursively load subtrees (directories)
    for (auto child : node->children) {
        if (child->isDir) {
            TreeNode *sub = loadTreeRecursive(child->sha1);
            // transfer subtree children (avoid double-free by clearing subtree object children container after move)
            child->children = sub->children;
            sub->children.clear();
            delete sub;
        }
    }
    return node;
}

// read blob content (decompressed body only)
static string readBlobContent(const string &blobHex) {
    string decomp = readObjectDecompressed(blobHex);
    size_t nul = decomp.find('\0');
    if (nul == string::npos) throw runtime_error("readBlobContent: malformed blob");
    return decomp.substr(nul + 1);
}

// ----------------- Graph helpers: parents and LCA -----------------

// get parents of commit (hex strings)
static vector<string> get_parents_of_commit(const string &commitHex) {
    string decompressed = readObjectDecompressed(commitHex);
    size_t nul = decompressed.find('\0');
    if (nul == string::npos) return {};
    string body = decompressed.substr(nul + 1);
    vector<string> parents;
    istringstream iss(body);
    string line;
    while (getline(iss, line)) {
        if (line.rfind("parent ", 0) == 0) {
            string p = line.substr(7);
            if (p.size() == 40) parents.push_back(p);
        } else if (line.empty()) break;
    }
    return parents;
}

// Bidirectional BFS for LCA with caching
static string findLCA(const string &a, const string &b) {
    if (a == b) return a;
    unordered_set<string> visA, visB;
    queue<string> qA, qB;
    unordered_map<string, vector<string>> cache;

    auto getParents = [&](const string &h)->const vector<string>& {
        auto it = cache.find(h);
        if (it != cache.end()) return it->second;
        vector<string> p = get_parents_of_commit(h);
        cache.emplace(h, move(p));
        return cache[h];
    };

    qA.push(a); visA.insert(a);
    qB.push(b); visB.insert(b);

    while (!qA.empty() || !qB.empty()) {
        if (!qA.empty()) {
            int sz = qA.size();
            for (int i = 0; i < sz; ++i) {
                string cur = qA.front(); qA.pop();
                for (const string &p : getParents(cur)) {
                    if (visB.count(p)) return p;
                    if (!visA.count(p)) { visA.insert(p); qA.push(p); }
                }
            }
        }
        if (!qB.empty()) {
            int sz = qB.size();
            for (int i = 0; i < sz; ++i) {
                string cur = qB.front(); qB.pop();
                for (const string &p : getParents(cur)) {
                    if (visA.count(p)) return p;
                    if (!visB.count(p)) { visB.insert(p); qB.push(p); }
                }
            }
        }
    }
    return string();
}

// ----------------- Tree flattening & merge -----------------

// collect files into map<path, shaHex>
static void collectFiles(TreeNode *node, const string &prefix, unordered_map<string,string> &out) {
    if (!node) return;
    for (auto child : node->children) {
        string path = prefix.empty() ? child->name : prefix + "/" + child->name;
        if (child->isDir) collectFiles(child, path, out);
        else out[path] = child->sha1;
    }
}

struct MergeResult {
    unordered_map<string,string> merged; // path -> sha
    unordered_set<string> conflicts;
};

// 3-way simple merge decision (flag conflict when both changed and differ)
static MergeResult mergeFileMaps(const unordered_map<string,string> &base,
                                 const unordered_map<string,string> &src,
                                 const unordered_map<string,string> &tgt) {
    MergeResult res;
    unordered_set<string> all;
    for (auto &p : base) all.insert(p.first);
    for (auto &p : src)  all.insert(p.first);
    for (auto &p : tgt)  all.insert(p.first);

    for (auto &path : all) {
        string b = base.count(path) ? base.at(path) : string();
        string s = src.count(path)  ? src.at(path)  : string();
        string t = tgt.count(path)  ? tgt.at(path)  : string();

        // both removed
        if (s.empty() && t.empty()) continue;

        if (!s.empty() && !t.empty() && s == t) {
            res.merged[path] = s;
        }
        else if (s == b && !t.empty()) {
            // only target changed
            res.merged[path] = t;
        }
        else if (t == b && !s.empty()) {
            // only source changed
            res.merged[path] = s;
        }
        else if (b.empty() && !s.empty() && t.empty()) {
            // added in src
            res.merged[path] = s;
        }
        else if (b.empty() && !t.empty() && s.empty()) {
            // added in tgt
            res.merged[path] = t;
        }
        else {
            // conflict (either both changed differently, or some ambiguous case)
            res.conflicts.insert(path);
        }
    }

    return res;
}

// build a TreeNode tree from map<path, sha>
static TreeNode* buildMergedTree(const unordered_map<string,string> &mergedMap) {
    TreeNode *root = new TreeNode("", "", true);
    for (auto &kv : mergedMap) {
        const string &path = kv.first;
        const string &sha  = kv.second;
        vector<string> parts;
        string tmp;
        stringstream ss(path);
        while (getline(ss, tmp, '/')) if (!tmp.empty()) parts.push_back(tmp);

        TreeNode *cur = root;
        for (size_t i = 0; i < parts.size(); ++i) {
            bool isLeaf = (i + 1 == parts.size());
            // find child
            TreeNode *next = nullptr;
            for (auto c : cur->children) if (c->name == parts[i]) { next = c; break; }
            if (!next) {
                next = new TreeNode("", parts[i], !isLeaf);
                cur->children.push_back(next);
            }
            cur = next;
            if (isLeaf) cur->sha1 = sha;
        }
    }
    return root;
}

// ----------------- Merge high-level -----------------

// Create conflict blob for a path given src/tgt blob hex. Returns new blob hex.
static string createConflictBlob(const string &srcHex, const string &tgtHex, const string &path) {
    string srcContent = srcHex.empty() ? string() : readBlobContent(srcHex);
    string tgtContent = tgtHex.empty() ? string() : readBlobContent(tgtHex);

    string mergedText;
    mergedText += "<<<<<<< SOURCE\n";
    mergedText += srcContent;
    if (!srcContent.empty() && srcContent.back() != '\n') mergedText += "\n";
    mergedText += "=======\n";
    mergedText += tgtContent;
    if (!tgtContent.empty() && tgtContent.back() != '\n') mergedText += "\n";
    mergedText += ">>>>>>> TARGET\n";

    // create blob object
    string blobHex = writeBlobObjectFromString(mergedText);
    return blobHex;
}

// Merge three tree hexes (base, source, target). Returns merged tree hex and sets conflicts list
static pair<string, vector<string>> mergeTrees(const string &baseTreeHex,
                                              const string &srcTreeHex,
                                              const string &tgtTreeHex) {
    // load recursive trees
    TreeNode *baseRoot = nullptr;
    TreeNode *srcRoot  = nullptr;
    TreeNode *tgtRoot  = nullptr;
    try {
        baseRoot = loadTreeRecursive(baseTreeHex);
        srcRoot  = loadTreeRecursive(srcTreeHex);
        tgtRoot  = loadTreeRecursive(tgtTreeHex);
    } catch (const exception &e) {
        // cleanup partial allocations
        if (baseRoot) { /* free tree */ }
        if (srcRoot)  { /* free tree */ }
        if (tgtRoot)  { /* free tree */ }
        throw;
    }

    unordered_map<string,string> baseFiles, srcFiles, tgtFiles;
    collectFiles(baseRoot, "", baseFiles);
    collectFiles(srcRoot,  "", srcFiles);
    collectFiles(tgtRoot,  "", tgtFiles);

    MergeResult mr = mergeFileMaps(baseFiles, srcFiles, tgtFiles);

    // produce final map: for conflicts, produce conflict blob and set sha accordingly
    unordered_map<string,string> finalMap = mr.merged;
    vector<string> conflictPaths;
    for (const auto &p : mr.conflicts) {
        string srcHex = srcFiles.count(p) ? srcFiles[p] : string();
        string tgtHex = tgtFiles.count(p) ? tgtFiles[p] : string();
        string conflictBlobHex = createConflictBlob(srcHex, tgtHex, p);
        finalMap[p] = conflictBlobHex;
        conflictPaths.push_back(p);
    }

    // build merged tree and compute tree hash using your computeTreeHash
    TreeNode *mergedRoot = buildMergedTree(finalMap);
    string mergedTreeHex = computeTreeHash(mergedRoot);

    // cleanup trees (free memory)
    std::function<void(TreeNode*)> freeTree = [&](TreeNode *n){
        if (!n) return;
        for (auto c : n->children) freeTree(c);
        delete n;
    };
    freeTree(baseRoot);
    freeTree(srcRoot);
    freeTree(tgtRoot);
    freeTree(mergedRoot);

    return {mergedTreeHex, conflictPaths};
}

// ----------------- Top-level merge command -----------------
// Merge branch `targetBranch` into current HEAD branch (HEAD resolves to commit or ref)
int merge_branch(const string &targetBranch) {
    // locate ref file for target
    fs::path targetRef = repoPath / "refs" / "heads" / targetBranch;
    if (!fs::exists(targetRef)) {
        cerr << "merge: " << targetBranch << " - not something we can merge\n";
        return 1;
    }

    // resolve HEAD to a commit hash
    // HEAD can be plain commit hex or "ref: refs/heads/<name>" — mimic your commit.cpp behavior
    string headContent;
    {
        fs::path headPath = repoPath / "HEAD";
        if (!fs::exists(headPath)) {
            cerr << "merge: no HEAD\n"; return 1;
        }
        ifstream hf(headPath);
        if (!hf) { cerr << "merge: cannot read HEAD\n"; return 1; }
        getline(hf, headContent);
        hf.close();
        // trim
        auto l = headContent.find_first_not_of(" \t\r\n");
        if (l == string::npos) { cerr << "merge: empty HEAD\n"; return 1; }
        auto r = headContent.find_last_not_of(" \t\r\n");
        headContent = headContent.substr(l, r - l + 1);
    }

    string headCommit;
    if (headContent.rfind("ref:", 0) == 0 || headContent.rfind("ref: ", 0) == 0) {
        size_t colon = headContent.find(':');
        string refSub = headContent.substr(colon + 1);
        // trim
        refSub.erase(0, refSub.find_first_not_of(" \t\r\n"));
        refSub.erase(refSub.find_last_not_of(" \t\r\n") + 1);
        fs::path refPath = repoPath / refSub;
        if (!fs::exists(refPath)) { cerr << "merge: HEAD ref not found\n"; return 1; }
        headCommit = getCommitHashFromFile(refPath);
    } else {
        headCommit = headContent;
    }
    if (headCommit.empty()) { cerr << "merge: HEAD not pointing to commit\n"; return 1; }

    // target commit
    string targetCommit = getCommitHashFromFile(targetRef);
    if (targetCommit.empty()) { cerr << "merge: target branch has no commit\n"; return 1; }

    if (headCommit == targetCommit) {
        cerr << "merge: " << targetBranch << " - already up-to-date\n";
        return 1;
    }

    // find LCA commit
    string lca = findLCA(headCommit, targetCommit);
    if (lca.empty()) {
        cerr << "merge: no common ancestor found\n"; return 1;
    }

    // get tree hashes from commits
    string baseTreeHex, headTreeHex, targetTreeHex;
    try {
        string baseDecomp = readObjectDecompressed(lca);
        baseTreeHex = parseCommitObject(baseDecomp).first;

        string headDecomp = readObjectDecompressed(headCommit);
        headTreeHex = parseCommitObject(headDecomp).first;

        string tgtDecomp = readObjectDecompressed(targetCommit);
        targetTreeHex = parseCommitObject(tgtDecomp).first;
    } catch (const exception &e) {
        cerr << "merge: failed to read commit objects: " << e.what() << "\n";
        return 1;
    }

    // merge trees
    pair<string, vector<string>> mergeResult;
    try {
        mergeResult = mergeTrees(baseTreeHex, headTreeHex, targetTreeHex);
    } catch (const exception &e) {
        cerr << "merge: mergeTrees failed: " << e.what() << "\n";
        return 1;
    }

    string mergedTreeHex = mergeResult.first;
    vector<string> conflicts = mergeResult.second;

    // create commit with two parents (HEAD, target)
    vector<string> parents = { headCommit, targetCommit };
    string msg = string("Merge branch ") + targetBranch + " into " + string("HEAD");
    string mergedCommitHex = createCommitObject(mergedTreeHex, parents, msg);

    // update HEAD and branch ref (call your updateHead)
    // need to figure current branch name (search refs/heads for headCommit)
    string currentBranch;
    for (const auto &entry : fs::directory_iterator(repoPath / "refs" / "heads")) {
        if (!entry.is_regular_file()) continue;
        string val = getCommitHashFromFile(entry.path());
        if (val == headCommit) { currentBranch = entry.path().filename().string(); break; }
    }
    if (currentBranch.empty()) {
        // HEAD detached; we just write HEAD directly
        // updateHead will write to HEAD or ref depending on its content
        updateHead(mergedCommitHex, "HEAD"); // updateHead expects branch name; if HEAD is detached, it writes HEAD directly
    } else {
        updateHead(mergedCommitHex, currentBranch);
    }

    // Report result
    cout << "Merge completed. New commit: " << mergedCommitHex << "\n";
    if (!conflicts.empty()) {
        cout << "Conflicts detected in " << conflicts.size() << " files:\n";
        for (auto &p : conflicts) cout << "  " << p << "\n";
        cout << "Conflict files were written with conflict markers into the index (and tree).\n";
    }

    return 0;
}

