#pragma once
#include "ShapeModel.h"
#include <atomic>
#include <cstdint>

// Lock-free SPSC (single-producer/single-consumer) triple buffer for handing baked
// ShapeTables from the message thread (producer) to the audio thread (consumer)
// without the audio thread ever blocking, allocating, or seeing a torn/partial table.
// Pure C++, no JUCE.
//
// Design (classic "triple buffering", e.g. Fabian Giesen's write-up): 3 slots are
// partitioned into exactly one producer-owned write slot, one consumer-owned read
// slot, and one shared "middle" slot, tracked via a single atomic<uint8_t> that packs
// a 2-bit slot index with a 1-bit "dirty" (unread) flag. publish() and acquire() each
// perform ONE atomic exchange on that byte:
//   - publish(): producer offers its write slot as the new shared/dirty slot, and
//     takes back whatever slot was previously shared (marked dirty or not -- doesn't
//     matter, the old shared value is fully consumed either way) as its NEXT write
//     target. That slot is guaranteed not to be the consumer's read slot, because the
//     consumer's read slot is never placed into `shared` by anyone but the consumer
//     itself (acquire() only ever publishes ITS OWN old read slot back into `shared`).
//   - acquire(): if `shared` is marked dirty, the consumer swaps its own (now-stale)
//     read slot into `shared` (clearing the dirty bit) and takes the dirty slot as its
//     new read slot. If `shared` isn't dirty, the consumer's current read slot is
//     already the latest -- no swap needed, so repeated acquire() calls with no
//     intervening publish() return the SAME pointer (verified by test).
// Because the 3 indices {writeIdx (producer-local), readIdx (consumer-local), shared
// (atomic)} always partition {0,1,2}, the producer can never write into the slot the
// consumer currently holds, and vice versa -- no locks, no torn reads, no allocation.
namespace lflow {

struct ShapeTable { float data[kShapeTableSize]; };

class ShapeTableBuffer
{
public:
    ShapeTableBuffer() noexcept = default;

    // Producer-side scratch space: write the next table's contents here, then call
    // publish(). Only ever called from the producer (message) thread.
    ShapeTable& writeBuffer() noexcept { return slots[writeIdx]; }

    // Publishes writeBuffer()'s current contents as the latest table and picks up a
    // new (unshared, safe-to-overwrite) scratch slot for the next write. Producer-only.
    void publish() noexcept
    {
        const auto newShared = static_cast<std::uint8_t> (writeIdx | kDirtyFlag);
        const std::uint8_t old = shared.exchange (newShared, std::memory_order_acq_rel);
        writeIdx = static_cast<int> (old & kIndexMask);
        everPublished.store (true, std::memory_order_release);
    }

    // Returns the latest published table. The returned pointer is stable (its
    // contents will not change) until the NEXT call to acquire() -- safe to hold and
    // read from for an entire audio block. Consumer-only. Before the first publish(),
    // returns a valid pointer to a zero-initialized slot; check hasEverPublished() to
    // know whether the contents are meaningful.
    const ShapeTable* acquire() noexcept
    {
        const std::uint8_t s = shared.load (std::memory_order_acquire);
        if (s & kDirtyFlag)
        {
            const auto newShared = static_cast<std::uint8_t> (readIdx);
            const std::uint8_t old = shared.exchange (newShared, std::memory_order_acq_rel);
            readIdx = static_cast<int> (old & kIndexMask);
        }
        return &slots[readIdx];
    }

    bool hasEverPublished() const noexcept { return everPublished.load (std::memory_order_acquire); }

private:
    static constexpr std::uint8_t kIndexMask = 0x3u;
    static constexpr std::uint8_t kDirtyFlag = 0x4u;

    ShapeTable slots[3] {};
    int writeIdx { 0 };
    int readIdx  { 1 };
    std::atomic<std::uint8_t> shared { 2 }; // slot 2, not dirty: nothing published yet
    std::atomic<bool> everPublished { false };
};

} // namespace lflow
