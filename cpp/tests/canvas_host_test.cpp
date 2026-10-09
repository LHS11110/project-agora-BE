#include "CanvasHostElection.hpp"
#include <cassert>
int main() {
    assert(electCanvasHost({{"a", false, true, {"default"}}, {"z", false, true, {"default"}}}) == "z");
    assert(electCanvasHost({{"a", true, true, {"default"}}, {"z", false, true, {"private"}}}) == "a");
    assert(electCanvasHost({{"a", false, true, {"one"}}, {"z", false, true, {"two"}}}).empty());
    assert(electCanvasHost({{"a", true, true, {}}, {"z", false, false, {"default"}}}).empty());
    assert(canvasHostMayPersist("z", "term-2", "z", "term-2"));
    assert(!canvasHostMayPersist("z", "term-2", "a", "term-2"));
    assert(!canvasHostMayPersist("z", "term-2", "z", "term-1"));
    assert(!canvasHostMayPersist("", "term-2", "z", "term-2"));
}
