#include <iostream>
#include <filesystem>
#include <fstream>

using namespace std;
namespace fs = filesystem;

int mintvcs_init() {
    fs::path repoPath = ".mintvcs"; 

    try {
        bool newlyCreated = false;
        if (!fs::exists(repoPath)) {
            fs::create_directory(repoPath);
            newlyCreated = true;
        }

        if (!fs::exists(repoPath / "objects"))
            fs::create_directory(repoPath / "objects");

        if (!fs::exists(repoPath / "refs"))
            fs::create_directory(repoPath / "refs");

        if (!fs::exists(repoPath / "refs/heads"))
            fs::create_directories(repoPath / "refs/heads");

        if (!fs::exists(repoPath / "refs/tags"))
            fs::create_directory(repoPath / "refs/tags");

        if (!fs::exists(repoPath / "HEAD")) {
            ofstream headFile(repoPath / "HEAD");
            if (headFile.is_open()) {
                headFile << "ref: refs/heads/main\n";
                headFile.close();
            }
            newlyCreated = true;
        }

        if (!fs::exists(repoPath / "config")) {
            ofstream configFile(repoPath / "config");
            if (configFile.is_open()) {
                configFile << "[core]\n";
                configFile << "\trepositoryformatversion = 0\n";
                configFile << "\tfilemode = false\n";
                configFile << "\tbare = false\n";
                configFile.close();
            }
            newlyCreated = true;
        }

        if (!fs::exists(repoPath / "description")) {
            ofstream descriptionFile(repoPath / "description");
            if (descriptionFile.is_open()) {
                descriptionFile << "Unnamed repository; edit this file to name it.\n";
                descriptionFile.close();
            }
            newlyCreated = true;
        }

        if (!fs::exists(repoPath / ".mintvcsignore"))
            fs::create_directory(repoPath / ".mintvcsignore");

        if (newlyCreated)
            cout << "Initialized empty MintVCS repository in " << fs::absolute(repoPath) << endl;
        else
            cout << "Reinitialized existing MintVCS repository in " << fs::absolute(repoPath) << endl;
    } 
    catch (const std::exception &e) {
        cerr << "Error initializing MintVCS repository: " << e.what() << endl;
        return 1;
    }

    return 0;
}
