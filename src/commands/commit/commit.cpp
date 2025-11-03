#include <iostream>
#include <filesystem>
#include <fstream>
#include<vector>
#include <cstdint>
#include<string>
#include<sstream>
#include<algorithm>
#include<ctime>
#include "../hash_object/hash_object.h"
#include "commit.h"

using namespace std;
namespace fs = filesystem;

static string sha1_hex(const string &s) {
    vector<uint8_t> v(s.begin(), s.end());
    return sha1_hex_of_bytes(v);
}

static void storeObject(const string &oid_hex, const string &full_content) {
    vector<uint8_t> raw(full_content.begin(), full_content.end());
    auto compressed = zlib_compress_bytes(raw);
    write_object_file(oid_hex, compressed);
}


struct IndexEntry {
    string mode;
    string type;     
    string sha1;
    string path;
    string mtime;
};

vector<IndexEntry> read_index(string path){
    ifstream f(path);
    if (!f) {
        cerr<<"Cannot open index file"<<endl;
        return {};
    }

    vector<IndexEntry> entries;
    string line;

    while(getline(f, line)){
        if(line.empty()) continue;

        stringstream ss(line);
        IndexEntry entry;
        ss >> entry.mode >> entry.type >> entry.sha1 >> entry.path >> entry.mtime;
        entries.push_back(entry);
    }

    return entries;
}

class TreeNode{
public:
    string sha1;
    string name;
    bool isDir;
    vector<TreeNode*> children;

    TreeNode(string sha1, string name, bool isDir): sha1(sha1), name(name), isDir(isDir) {}
};

vector<string> splitPath(const string& path){
    vector<string> parts;
    stringstream ss(path);
    string segment;

    while(getline(ss, segment, '/')){
        if(!segment.empty()){
            parts.push_back(segment);
        }
    }

    return parts;
}


string computeTreeHash(TreeNode* root){
    if(!root->isDir) return root->sha1;


    string content = "";
    for(auto &child: root->children){
        string childHash = computeTreeHash(child);
        child->sha1 = childHash;

        string mode = child->isDir ? "40000" : "100644";
        content += mode + " " + child->name + "\0" + childHash; 
    }
    string header = "tree " + to_string(content.size()) + "\0";
    string full = header + content;
    string treeHash = sha1_hex(full);
    storeObject(treeHash, full);
    return treeHash;
}

TreeNode build_tree(vector<IndexEntry> entries){
    TreeNode root("", "", true);
    for(auto &e: entries){
        vector<string> parts = splitPath(e.path);
        TreeNode* current = &root;
        
        
        for (size_t i=0;i<parts.size();++i){
            const string& part = parts[i];
            bool isLeaf = (i== parts.size()-1); 

            // Check if child already exists
            auto it = find_if(current->children.begin(), current->children.end(),
                [&](TreeNode* n) { return n->name == part; });


            TreeNode* next = nullptr;

            if (it == current->children.end()) {
                TreeNode* newNode = new TreeNode("", part, !isLeaf);
                current->children.push_back(newNode);
                next = newNode;
            } else {
                next = *it;
            }

            current = next;
            if (isLeaf) current->sha1 = e.sha1;  
        }
    }
    return root;
}

string createCommitObject(string treeHash, string parentHash, string message){
    ostringstream body;
    body << "tree "<<treeHash<<"\n";
    if(parentHash!="") body << "parent "<<parentHash<<"\n";
    body << message << "\n";

    string commitContent = body.str();
    string header = "commit " + to_string(commitContent.size()) + "\0";
    string full_content = header + commitContent;

    string commit_hash = sha1_hex(full_content);
    storeObject(commit_hash, commitContent);

    return commit_hash;
}


void updateHead(string hash){
    ofstream head(".mintvcs/Head", ios::trunc);
    head << hash << "\n";
    head.close();
}


int mintvcs_commit(const string message) {
    fs::path repoPath = ".mintvcs"; 
    fs::path index_path = ".mintvcs/index";

    try {
        if (!fs::exists(index_path)) {
            cerr << "Index not found: " << index_path << "\n";
            return 1;
        }

        auto entries = read_index(".mintvcs/index");
        if (entries.empty()) {
            cerr << "Index empty. Nothing to commit.\n";
            return 1;
        }

        // sort lexicographically by path
        sort(entries.begin(), entries.end(), [](const IndexEntry &a, const IndexEntry &b){
            return a.path < b.path;
        });

        TreeNode root = build_tree(entries);
        string root_tree_oid = computeTreeHash(&root);
        // parent: read .mintvcs/HEAD if exists
        string parent = "";
        fs::path headpath = ".mintvcs/HEAD";
        if (fs::exists(headpath)) {
            ifstream hf(headpath);
            if (hf) {
                string line;
                getline(hf, line);
                hf.close();
                if (!line.empty()) parent = line;
            }
        }

        string commit_oid = createCommitObject(root_tree_oid, parent, message);
        updateHead(commit_oid);

        cout << "Created commit " << commit_oid << "\n";
        return 0;
    } catch (const exception &ex) {
        cerr << "commit failed: " << ex.what() << "\n";
        return 1;
    }
}

void commit(const string& message) {
    mintvcs_commit(message);
}