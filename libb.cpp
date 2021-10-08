#include "singleton.hpp"
#include <iostream>
using namespace std;

extern "C" {
    int get() {
        cout << "libb get" << endl << flush;
        Cache::ConstAccessor accessor;
        getCache().find(accessor, 42);

        return *accessor;
    }
}
