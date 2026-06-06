// Behavioral tests for the EventBucket family (eta_hsm/utils/EventBucket.hpp).
// Tests exercise the public interface directly: OrderedEventBucket preserves insertion order,
// PrioritizedEventBucket orders by enumerator priority, and the removing accessor
// (getEvent) reports eNone when drained, while top() requires a non-empty bucket.
// Exercises only the public surface and observable behavior.
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

TEST(EventBucketTest, PrioritizedBucketGetEventReturnsNoneWhenEmpty)
{
    PrioritizedEventBucket<Event> bucket;
    EXPECT_EQ(bucket.getEvent(), Event::eNone);
}

// top() carries a pre(!empty()) precondition: peeking an empty bucket has no
// defined value, so the contract traps rather than fabricating a sentinel.
// Built under -fcontract-evaluation-semantic=enforce, a violation aborts. The
// matcher pins the predicate text the handler echoes, so it proves this
// precondition fired rather than any abort.
TEST(EventBucketDeathTest, PrioritizedBucketTopOnEmptyViolatesPrecondition)
{
    PrioritizedEventBucket<Event> bucket;
    EXPECT_DEATH((void)bucket.top(), "contract violation.*!empty");
}

TEST(EventBucketTest, PrioritizedBucketTopPeeksHighestPriorityWithoutRemoving)
{
    PrioritizedEventBucket<Event> bucket;
    bucket.addEvent(Event::eLow);
    bucket.addEvent(Event::eHigh);

    EXPECT_EQ(bucket.top(), Event::eHigh);
    EXPECT_EQ(bucket.size(), 2u);  // top() peeks; it does not pop
}

}  // namespace tests
}  // namespace utils
}  // namespace eta_hsm
