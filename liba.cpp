#include "singleton.hpp"
#include <iostream>
using namespace std;

extern "C" {
    void add() {
        cout << "liba add" << endl << flush;
        getCache().insert(42, 42);
    }
}
