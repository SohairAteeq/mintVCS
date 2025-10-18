#include <iostream>
#include <cstring>
using namespace std;

string mint_init(){
    return "success";
}

int main(int argc, char* argv[]) {

    if(strcmp(argv[1], "mintvcs") == 0){
        if(strcmp(argv[2], "init") == 0){
            string s = mint_init();
            cout<<s;
        }
    }

    return 0;
}

