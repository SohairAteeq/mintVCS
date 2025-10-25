#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include "./commands/init/init.h"
#include "./commands/hash_object/hash_object.h"
#include "./commands/add/add.h"
#include "./commands/commit/commit.h"

using namespace std;

int main(int argc, char* argv[]) {
    cout << "hello" << endl;

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
    else if(strcmp(argv[1], "add")==0){
        vector<string> paths;
        for (int i = 1; i < argc; ++i) {
            paths.push_back(argv[i]);
        }
        add(paths);
    }
    else if(strcmp(argv[1], "commit")==0){
        string message = argv[2];
        commit(message);
    }
    else {
        cout << "Unknown command: " << argv[1] << endl;
    }

    return 0;
}
