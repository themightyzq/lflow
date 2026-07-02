#include <catch2/catch_test_macros.hpp>
#include "TripleBuffer.h"
#include <thread>
#include <atomic>

using namespace lflow;

namespace {

void fillPattern (ShapeTable& t, float v) noexcept
{
    for (float& f : t.data) f = v;
}

bool allEqual (const ShapeTable& t, float v) noexcept
{
    for (float f : t.data)
        if (f != v) return false;
    return true;
}

} // namespace

TEST_CASE ("TripleBuffer: before any publish, hasEverPublished is false and acquire is stable", "[triplebuffer]")
{
    ShapeTableBuffer buf;
    REQUIRE_FALSE (buf.hasEverPublished());

    const ShapeTable* p1 = buf.acquire();
    const ShapeTable* p2 = buf.acquire();
    REQUIRE (p1 == p2); // no publish happened between the two acquires -> same slot returned
}

TEST_CASE ("TripleBuffer: publish then acquire returns the just-published contents", "[triplebuffer]")
{
    ShapeTableBuffer buf;
    fillPattern (buf.writeBuffer(), 1.0f);
    buf.publish();
    REQUIRE (buf.hasEverPublished());

    const ShapeTable* p = buf.acquire();
    REQUIRE (allEqual (*p, 1.0f));
}

TEST_CASE ("TripleBuffer: repeated publishes without an intervening acquire keep only the newest", "[triplebuffer]")
{
    ShapeTableBuffer buf;
    for (int i = 1; i <= 5; ++i)
    {
        fillPattern (buf.writeBuffer(), static_cast<float> (i));
        buf.publish();
    }

    const ShapeTable* p = buf.acquire();
    REQUIRE (allEqual (*p, 5.0f)); // only the last publish survives to be observed
}

TEST_CASE ("TripleBuffer: consumer's held slot is never overwritten by later publishes", "[triplebuffer]")
{
    ShapeTableBuffer buf;

    fillPattern (buf.writeBuffer(), 1.0f);
    buf.publish();
    const ShapeTable* held = buf.acquire();
    REQUIRE (allEqual (*held, 1.0f));

    // Producer publishes twice more WITHOUT the consumer re-acquiring.
    fillPattern (buf.writeBuffer(), 2.0f);
    buf.publish();
    fillPattern (buf.writeBuffer(), 3.0f);
    buf.publish();

    // The classic 3-slot swap must never let the producer target the slot the
    // consumer is still holding: `held`'s contents must be untouched.
    REQUIRE (allEqual (*held, 1.0f));

    // A fresh acquire jumps straight to the newest publish, never landing on the
    // stale intermediate 2.0f.
    const ShapeTable* held2 = buf.acquire();
    REQUIRE (allEqual (*held2, 3.0f));
}

TEST_CASE ("TripleBuffer: repeated acquire()s with no intervening publish return the same pointer", "[triplebuffer]")
{
    ShapeTableBuffer buf;
    fillPattern (buf.writeBuffer(), 7.0f);
    buf.publish();

    const ShapeTable* a = buf.acquire();
    const ShapeTable* b = buf.acquire();
    const ShapeTable* c = buf.acquire();
    REQUIRE (a == b);
    REQUIRE (b == c);
}

// Optional 2-thread hammer test (brief: "acceptable if deterministic assertions
// only"). We don't assert WHICH tables the consumer sees (that's timing-dependent),
// only properties that must hold under any legal interleaving: every acquired table
// is internally consistent (whole-slot swap, never torn/mixed), and sentinel values
// never regress (single producer publishes strictly increasing sentinels in order,
// so the consumer can only observe a non-decreasing sequence).
TEST_CASE ("TripleBuffer: 2-thread hammer -- consumer never sees a torn table or a regressed value", "[triplebuffer][hammer]")
{
    ShapeTableBuffer buf;
    std::atomic<bool> producerDone { false };
    constexpr int kIterations = 20000;

    std::thread producer ([&]
    {
        for (int i = 1; i <= kIterations; ++i)
        {
            fillPattern (buf.writeBuffer(), static_cast<float> (i));
            buf.publish();
        }
        producerDone.store (true, std::memory_order_release);
    });

    float lastSeen = 0.0f;
    bool sawAny = false;
    while (! producerDone.load (std::memory_order_acquire))
    {
        if (! buf.hasEverPublished())
            continue;
        const ShapeTable* p = buf.acquire();
        const float v = p->data[0];
        REQUIRE (allEqual (*p, v));      // internally consistent: never a torn/mixed table
        if (sawAny)
            REQUIRE (v >= lastSeen);     // monotonic non-decreasing
        lastSeen = v;
        sawAny = true;
    }
    producer.join();

    const ShapeTable* last = buf.acquire();
    if (sawAny)
        REQUIRE (last->data[0] >= lastSeen);
}
