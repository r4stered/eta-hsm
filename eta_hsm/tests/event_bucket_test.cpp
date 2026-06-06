// Behavioral tests for the EventBucket family (eta_hsm/utils/EventBucket.hpp).
// Tests exercise the public interface directly: OrderedEventBucket preserves insertion order,
// PrioritizedEventBucket orders by enumerator priority, and both report eNone
// when drained. Exercises only the public surface and observable behavior.
#include <gtest/gtest.h>

#include "eta_hsm/utils/EventBucket.hpp"

namespace eta_hsm {
namespace utils {
namespace tests {

// Lower enumerator value == higher priority for PrioritizedEventBucket; eNone
// sits at the bottom so it is the lowest priority and the empty-bucket sentinel.
enum class Event { eNone, eHigh, eMid, eLow };

TEST(EventBucketTest, OrderedBucketPreservesInsertionOrder)
{
    OrderedEventBucket<Event> bucket;
    EXPECT_TRUE(bucket.empty());

    bucket.addEvent(Event::eLow);
    bucket.addEvent(Event::eHigh);
    bucket.addEvent(Event::eMid);

    EXPECT_EQ(bucket.size(), 3u);
    EXPECT_EQ(bucket.getEvent(), Event::eLow);
    EXPECT_EQ(bucket.getEvent(), Event::eHigh);
    EXPECT_EQ(bucket.getEvent(), Event::eMid);
    EXPECT_TRUE(bucket.empty());
}

TEST(EventBucketTest, OrderedBucketReturnsNoneWhenDrained)
{
    OrderedEventBucket<Event> bucket;
    EXPECT_EQ(bucket.getEvent(), Event::eNone);

    bucket.addEvent(Event::eMid);
    EXPECT_EQ(bucket.getEvent(), Event::eMid);
    EXPECT_EQ(bucket.getEvent(), Event::eNone);
}

TEST(EventBucketTest, PrioritizedBucketOrdersByEnumeratorPriority)
{
    PrioritizedEventBucket<Event> bucket;

    // Added out of priority order; should come out highest-priority first
    // (smallest enumerator value first).
    bucket.addEvent(Event::eLow);
    bucket.addEvent(Event::eHigh);
    bucket.addEvent(Event::eMid);

    EXPECT_EQ(bucket.size(), 3u);
    EXPECT_EQ(bucket.getEvent(), Event::eHigh);
    EXPECT_EQ(bucket.getEvent(), Event::eMid);
    EXPECT_EQ(bucket.getEvent(), Event::eLow);
    EXPECT_TRUE(bucket.empty());
}

TEST(EventBucketTest, PrioritizedBucketReturnsNoneWhenEmpty)
{
    PrioritizedEventBucket<Event> bucket;
    EXPECT_EQ(bucket.top(), Event::eNone);
    EXPECT_EQ(bucket.getEvent(), Event::eNone);
}

}  // namespace tests
}  // namespace utils
}  // namespace eta_hsm
