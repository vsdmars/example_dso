#include "singleton.hpp"
#include <iostream>

extern "C" {
    void noop() {
        std::cout << "noop" << std::endl << std::flush;
    }
}
