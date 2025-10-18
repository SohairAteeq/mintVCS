// #include <iostream>
// #include <filesystem>
// #include <fstream>

// using namespace std;

// namespace fs = filesystem;

// int main() {
//     fs::path p = ".mint"; 

//     if (!fs::exists(p)) {
//         fs::create_directory(p);
//         fs::create_directory(".mint/objects");
//         fs::create_directories(".mint/refs/heads");
//         fs::create_directory(".mint/refs/tags");

//         ofstream headFile(".mint/HEAD");
//         if (headFile.is_open()) {
//             headFile << "ref: refs/heads/main\n";
//             headFile.close();
//         }

//         ofstream configFile(".mint/config");
//         if (configFile.is_open()) {
//             configFile << "[core]\n";
//             configFile << "\trepositoryformatversion = 0\n";
//             configFile << "\tfilemode = false\n";
//             configFile << "\tbare = false\n";
//             configFile.close();
//         }

//         ofstream descriptionFile(".mint/description");
//         if (descriptionFile.is_open()) {
//             descriptionFile << "Unnamed repository; edit this file to name it.\n";
//             descriptionFile.close();
//         }

//         cout << "Initialized empty MintVCS repository in " << fs::absolute(p) << endl;
//     } 
//     else {
//         cout << "Reinitialized existing MintVCS repository in " << fs::absolute(p) << endl;
//     }
    
//     return 0;
// }