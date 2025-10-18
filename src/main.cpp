#include <iostream>
#include <cstring>
#include "./commands/init/init.h"
using namespace std;

string mint_init(){
    return "success";
}

int main(int argc, char* argv[]) {

    if(strcmp(argv[1], "mintvcs") == 0){
        if(strcmp(argv[2], "init") == 0){
            mintvcs_init();
        }
    }

    return 0;
}

