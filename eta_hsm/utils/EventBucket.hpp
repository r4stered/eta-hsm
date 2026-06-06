// eta_hsm/utils/EventBucket.hpp
#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <queue>
#include <vector>

namespace eta_hsm {
namespace utils {

/// A bucket for carrying events around with a very simple interface.
template <typename Event>
class EventBucket {
public:
    virtual ~EventBucket() = default;

    /// All that is exposed publicly is the ability to add events.
    virtual void addEvent(Event evt) = 0;
};

/// We are free to implement lots of different types of EventBuckets that implement the interface declared above.
/// This particular example stores events (privately) in a container capable of preserving order, but it is up to
/// the user whether or not to assign any particular meaning to this order.
template <typename Event>
class OrderedEventBucket : public EventBucket<Event> {
public:
    /// Implement the addEvent interface declared in EventBucket.
    void addEvent(Event evt) override { mStorage.push_back(evt); }

    /// Empty the bucket.
    void clear() { mStorage.clear(); }

    /// Is the bucket empty?
    bool empty() { return mStorage.empty(); }

    /// How many events are in the bucket?
    size_t size() { return mStorage.size(); }

    /// Simplified accessor that removes an event from the bucket and returns it.
    Event getEvent()
    {
        if (!empty())
        {
            Event evt = mStorage.front();  // make a local copy
            mStorage.pop_front();
            return evt;
        }
        else
        {
            return Event::eNone;  // assuming there is an eNone element
        }
    }

    /// Direct access to underlying deque.
    Event front() { return mStorage.front(); }
    Event back() { return mStorage.back(); }

    /// Direct access to underlying deque.
    void pop_front() { mStorage.pop_front(); }
    void pop_back() { mStorage.pop_back(); }

    // TODO: add iterator accessors to facilitate easy looping over the contents

private:
    /// Private storage for events.  I am using a deque for now just because I can do this quickly.
    /// The whole point of this class is just to provide an interface behind which we can fiddle with implementations.
    std::deque<Event> mStorage{};
};

/// We can also build a version of EventBucket that uses a prioritized queue under the hood.
/// Note:  By default, priority_queue considers LARGER values to be higher priority, so the further
///        down the list of enums an event shows up, the HIGHER priority it is.
///        We change this by giving std::greater<T> as the comparator so that events closer to
///        the top of the list have higher priority.
///        Event::eNone should be at the bottom of the list so that it has the lowest possible priority.
template <typename Event>
class PrioritizedEventBucket : public EventBucket<Event> {
public:
    /// Implement the addEvent interface declared in EventBucket.
    void addEvent(Event evt) override { mStorage.push(evt); }

    /// Empty the bucket.
    void clear() { mStorage = {}; }

    /// Is the bucket empty?
    bool empty() const { return mStorage.empty(); }

    /// How many events are in the bucket?
    size_t size() const { return mStorage.size(); }

    /// Simplified accessor that removes an event from the bucket and returns it.
    Event getEvent()
    {
        if (!empty())
        {
            Event evt = mStorage.top();  // make a local copy
            mStorage.pop();
            return evt;
        }
        else
        {
            return Event::eNone;  // assuming there is an eNone element
        }
    }

    /// Peek the highest-priority event without removing it. Peeking an empty
    /// bucket has no defined value, so the precondition holds the caller to a
    /// non-empty bucket rather than fabricating a sentinel.
    Event top() pre(!empty()) { return mStorage.top(); }
    void pop() { mStorage.pop(); }

private:
    /// Private storage for events.  I am using a priority_queue to quickly sort events by priority.
    /// The whole point of this class is just to provide an interface behind which we can fiddle with implementations.
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> mStorage{};
};

}  // namespace utils
}  // namespace eta_hsm
