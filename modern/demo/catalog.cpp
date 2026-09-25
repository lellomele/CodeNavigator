#include "catalog.h"

namespace catalog {

// Keep the selected item available while the view changes.
int Catalog::findItem(int identifier) const {
    for (int index = 0; index < count; ++index) {
        if (items[index].identifier == identifier) {
            return index;
        }
    }
    return -1;
}

void Catalog::clear() {
    count = 0;
}

}
