#include "singleton.hpp"

Cache& getCache() noexcept {
    static Cache c{4242};

    return c;
}
