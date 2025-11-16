#include <iostream>
#include <string>
#include <vector>
#include <cstring>

#include "./commands/init/init.h"
#include "./commands/hash_object/hash_object.h"
#include "./commands/add/add.h"
#include "./commands/commit/commit.h"
#include "./commands/log/log.h"
#include "./commands/checkout/checkout.h"
#include "./commands/branch/branch.h"
#include "./commands/merge/merge.h"

using namespace std;

int main(int argc, char* argv[]) {

    if (argc < 2) {
        cout << "Usage: mintvcs <command> [args...]" << endl;
        return 1;
    }

    if (strcmp(argv[1], "init") == 0) {
        mintvcs_init();
    }
    else if (strcmp(argv[1], "hash-object") == 0) {
        if (argc > 3 && strcmp(argv[2], "-w") == 0) {
            cout << hash_object(argv[3], true);
        } else if (argc > 2) {
            cout << hash_object(argv[2], false);
        } else {
            cout << "Usage: mintvcs hash-object [-w] <file>" << endl;
        }
    }
    else if (strcmp(argv[1], "add") == 0) {
        vector<string> paths;
        for (int i = 2; i < argc; ++i) {
            paths.push_back(argv[i]);
        }
        add(paths);
    }
    else if (strcmp(argv[1], "commit") == 0) {
        // Fixed: properly parse -m flag
        string message;
        if (argc >= 4 && strcmp(argv[2], "-m") == 0) {
            message = argv[3];
        } else if (argc >= 3 && strcmp(argv[2], "-m") != 0) {
            // If no -m flag, treat all remaining args as message
            message = argv[2];
            for (int i = 3; i < argc; ++i) {
                message += " ";
                message += argv[i];
            }
        } else {
            cout << "Usage: mintvcs commit -m <message>" << endl;
            return 1;
        }
        commit(message);
    }
    else if (strcmp(argv[1], "log") == 0) {
        mintvcs_log();
    }
    else if (strcmp(argv[1], "checkout") == 0) {
        if (argc < 3) {
            cout << "Usage: mintvcs checkout <commit|branch>" << endl;
            return 1;
        }
        mintvcs_checkout(argv[2]);
    }
    else if(strcmp(argv[1], "branch")==0) {
        if(argc < 3) {
            cout << "Usage: mintvcs branch <create|list|delete|rename> [args...]" << endl;
            return 1;
        }
        if(strcmp(argv[2], "create")==0) {
            if(argc != 4) {
                cout << "Usage: mintvcs branch create <branch_name>" << endl;
                return 1;
            }
            createBranch(argv[3]);
        }
        else if(strcmp(argv[2], "list")==0) {
            listBranches();
        }
        else if(strcmp(argv[2], "delete")==0) {
            if(argc != 4) {
                cout << "Usage: mintvcs branch delete <branch_name>" << endl;
                return 1;
            }
            deleteBranch(argv[3]);
        }
        else if(strcmp(argv[2], "rename")==0) {
            if(argc != 5) {
                cout << "Usage: mintvcs branch rename <old_name> <new_name>" << endl;
                return 1;
            }
            renameBranch(argv[3], argv[4]);
        }
        else {
            cout << "Unknown branch command: " << argv[2] << endl;
            return 1;
        }
    }
    else if (strcmp(argv[1], "merge") == 0) {
        if (argc != 3) {
            cout << "Usage: mintvcs merge <branch_name>" << endl;
            return 1;
        }
        return merge_branch(argv[2]);
    }
    else {
        cout << "Unknown command: " << argv[1] << endl;
    }

    return 0;
}