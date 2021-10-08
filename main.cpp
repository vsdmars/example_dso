#include <dlfcn.h>
#include <iostream>

using namespace std;
using ADD_FUNC = void (*)();
using GET_FUNC = int (*)();
using NOOP_FUNC = ADD_FUNC;

int main() {
    cout << "start" << endl;
    auto handle_a = dlopen("./liba.so", RTLD_LAZY);
    auto handle_b = dlopen("./libb.so", RTLD_LAZY);
    auto handle_no = dlopen("./libno.so", RTLD_LAZY);

    if (!handle_a || !handle_b || !handle_no) {
        cout << "dlopen failed" << endl << flush;
    }



    ADD_FUNC add = reinterpret_cast<ADD_FUNC>(dlsym(handle_a, "add"));
    GET_FUNC get = reinterpret_cast<GET_FUNC>(dlsym(handle_b, "get"));
    NOOP_FUNC noop = reinterpret_cast<NOOP_FUNC>(dlsym(handle_no, "noop"));

    // cout << "dlsymed" << endl;

    add();
    // cout << "add" << endl;
    cout << get() << endl << flush;
    // cout << "get" << endl;
    noop();
    // cout << "noop" << endl;
}
