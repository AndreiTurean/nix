#include <nix/nix.hxx>
#include <cassert>
#include <iostream>

std::int32_t main() {
    // Capacity must be a power of 2 according to static_assert
    constexpr std::uint64_t testCapacity = 4;
    nix::SpscRingBuffer<std::int32_t, testCapacity> ringBuffer;

    // 1. Verify initial empty state
    assert(ringBuffer.empty() == true);
    assert(ringBuffer.size() == 0);
    assert(ringBuffer.capacity() == testCapacity);

    // 2. Push elements up to capacity
    assert(ringBuffer.push(100) == true);
    assert(ringBuffer.push(200) == true);
    assert(ringBuffer.push(300) == true);
    assert(ringBuffer.push(400) == true);

    // 3. Verify buffer is full and rejects further pushes
    assert(ringBuffer.empty() == false);
    assert(ringBuffer.size() == testCapacity);
    assert(ringBuffer.push(500) == false); // Should fail (full)

    // 4. Pop elements and verify FIFO ordering
    auto val1 = ringBuffer.pop();
    assert(val1.has_value() && *val1 == 100);

    auto val2 = ringBuffer.pop();
    assert(val2.has_value() && *val2 == 200);
    assert(ringBuffer.size() == 2);

    // 5. Test wrap-around behavior by pushing more items
    assert(ringBuffer.push(500) == true);
    assert(ringBuffer.push(600) == true);
    assert(ringBuffer.size() == 4);
    assert(ringBuffer.push(700) == false); // Full again

    // 6. Pop remaining elements in sequence
    assert(ringBuffer.pop() == 300);
    assert(ringBuffer.pop() == 400);
    assert(ringBuffer.pop() == 500);
    assert(ringBuffer.pop() == 600);

    // 7. Verify final empty state
    assert(ringBuffer.empty() == true);
    assert(ringBuffer.size() == 0);
    assert(ringBuffer.pop().has_value() == false);

    std::cout << "SpscRingBuffer test passed successfully!" << '\n';
    return 0;
}
