#pragma once

namespace catalog {
struct Item {
    int identifier;
    const char *name;
};

class Catalog {
public:
    int findItem(int identifier) const;
    void clear();
private:
    Item items[128];
    int count;
};
}
