#include "common/queue.h"

namespace pc {
// This TU exists so the object file is generated for the static library.
// Template implementations are in the header.
static const char* queue_anchor() {
    return typeid(BoundedQueue<int>).name();
}
} // namespace pc
