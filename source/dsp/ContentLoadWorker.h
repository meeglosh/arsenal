#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <memory>

namespace spa::dsp
{

// Cooperative cancellation token threaded through a background load job.
// The loader body polls shouldCancel() between chunks of work (retry
// attempts, analysis hops) and bails early once it's true -- set either
// because a newer request for the same slot/kind superseded this one while
// it was already running, or because the owning ContentLoadWorker is being
// torn down. A cancelled job's result is discarded exactly like a stale one
// always was: SPASynthProcessor's requestSerial latest-wins check at
// install time never installs it (see loadSampleFromFile/loadWavetableFromFile).
// Cancellation exists purely to stop wasting CPU/memory on work nobody
// wants any more, not as a correctness mechanism.
class CancelToken
{
public:
    bool shouldCancel() const noexcept { return flag.load (std::memory_order_relaxed); }
    void cancel() noexcept { flag.store (true, std::memory_order_relaxed); }

private:
    std::atomic<bool> flag { false };
};

// One background thread shared by every sample/wavetable content load in the
// processor. Before this existed, every quick-swap click (OscStrip's
// in-pack sample browser, preset loads restoring multiple slots, etc.) spawned
// its own detached juce::Thread that decoded and analyzed a full file --
// rapid auditioning could pile up an unbounded number of concurrent heavy
// jobs (thread exhaustion, CPU saturation, multi-GB memory, since each
// worst-case job decodes up to 10 minutes of audio and runs a YIN pitch
// analysis costing billions of ops).
//
// This worker replaces that with exactly one thread and a fixed grid of
// per-(kind, slot) mailboxes. Posting a new job for a key overwrites
// whatever was queued-but-not-yet-started for that key (latest-wins before
// any work begins -- the common case for rapid clicking, since the worker
// is usually still busy with the previous file), and if a job for that key
// is already RUNNING, its CancelToken is set so the loader bails at its
// next checkpoint instead of grinding on for a result nobody wants.
//
// This class only bounds concurrency/resource usage. Correctness (never
// installing a stale result) stays exactly as it was: governed entirely by
// SPASynthProcessor's requestSerial check on the message thread inside the
// job's own completion callback. Nothing about that changes here.
class ContentLoadWorker : private juce::Thread
{
public:
    ContentLoadWorker();
    ~ContentLoadWorker() override;

    enum class Kind { sample, wavetable };
    static constexpr int maxSlots = 4;   // matches params::maxOscSlots

    // The unit of work. Runs on the worker thread; must poll `cancel`
    // periodically for anything expensive and bail out early when it's set.
    using Job = std::function<void (CancelToken& cancel)>;

    struct PostedJob
    {
        Job run;
        // Invoked SYNCHRONOUSLY on the posting thread, inside post(), if this
        // job is replaced by a newer one before the worker ever starts it --
        // i.e. `run` above never executes at all for this request. Lets the
        // caller keep its own per-request bookkeeping (e.g.
        // SPASynthProcessor's pendingLoads counter, incremented once per
        // request) balanced even for requests that get superseded while still
        // queued, which never reach `run`'s own matching decrement.
        std::function<void()> onDiscarded;
    };

    // Posts a job keyed by (kind, slot). Message-thread only, matching
    // loadSampleFromFile/loadWavetableFromFile's existing contract.
    void post (Kind kind, int slot, PostedJob job);

    // Test/debug observability: how many jobs the worker actually STARTED
    // running (vs merely queued) and how many finished (completed or bailed
    // out via cancellation) -- NOT how many were requested. A request that's
    // superseded before the worker gets to it never increments either.
    int getStartedCount() const { return startedCount.load (std::memory_order_relaxed); }
    int getCompletedCount() const { return completedCount.load (std::memory_order_relaxed); }

private:
    void run() override;

    struct Mailbox
    {
        PostedJob pending;
        bool hasPending = false;
        std::shared_ptr<CancelToken> runningToken;   // set while a job for this key is running
    };

    static constexpr int numKinds = 2;

    juce::CriticalSection lock;
    std::array<std::array<Mailbox, (size_t) maxSlots>, (size_t) numKinds> mailboxes;

    std::atomic<int> startedCount { 0 };
    std::atomic<int> completedCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentLoadWorker)
};

} // namespace spa::dsp
