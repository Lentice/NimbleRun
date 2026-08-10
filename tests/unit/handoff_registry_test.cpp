#include "win/handoff_registry.h"

#include "test_util.h"

#include <memory>

namespace {

struct Value {
    explicit Value(int number_value) : number(number_value) {}
    int number;
};

void TestRegistry() {
    nimblerun::HandoffRegistry<Value> registry;
    auto first = std::make_unique<Value>(7);
    const auto token = registry.Register(std::move(first));
    Expect(token != 0, "register returns a token");
    auto taken = registry.Take(token);
    Expect(taken && taken->number == 7, "take returns the registered value");
    Expect(registry.Take(token) == nullptr, "taken value is removed");

    Expect(registry.Take(0x1234) == nullptr,
           "unknown token is ignored without dereference");
    const auto erased_token = registry.Register(std::make_unique<Value>(8));
    registry.Erase(erased_token);
    Expect(registry.Take(erased_token) == nullptr, "erase removes a token");

    const auto clear_one = registry.Register(std::make_unique<Value>(1));
    const auto clear_two = registry.Register(std::make_unique<Value>(2));
    registry.Clear();
    Expect(registry.Empty(), "clear releases every value");
    Expect(registry.Take(clear_one) == nullptr && registry.Take(clear_two) == nullptr,
           "clear releases all values");

    nimblerun::HandoffRegistry<Value> other;
    const auto other_token = other.Register(std::make_unique<Value>(9));
    Expect(registry.Take(other_token) == nullptr,
           "different registries do not share entries");
    Expect(other.Take(other_token) != nullptr,
           "other registry retains its entry");
}

} // namespace

int wmain() {
    TestRegistry();
    return 0;
}
