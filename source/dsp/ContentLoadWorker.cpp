#include "ContentLoadWorker.h"

namespace spa::dsp
{

ContentLoadWorker::ContentLoadWorker()
    : juce::Thread ("SPASynth Content Loader")
{
    startThread();
}

ContentLoadWorker::~ContentLoadWorker()
{
    // Drop anything still queued (nobody's left to run it for) and cancel
    // whatever's currently running so it bails at its next checkpoint
    // instead of stopThread() below blocking on a slow analysis. Queued
    // jobs are abandoned silently (onDiscarded is NOT called) -- the owning
    // processor is being torn down, there is no bookkeeping left to keep
    // consistent.
    {
        const juce::ScopedLock sl (lock);
        for (auto& row : mailboxes)
        {
            for (auto& mb : row)
            {
                mb.hasPending = false;
                mb.pending = {};
                if (mb.runningToken != nullptr)
                    mb.runningToken->cancel();
            }
        }
    }

    // signalThreadShouldExit() + notify() + join, in that order. A running
    // job (if any) should observe its cancelled token within one checkpoint
    // (a retry-loop sleep or a single analysis hop -- low milliseconds at
    // most) and return, after which run()'s own loop sees threadShouldExit()
    // and exits. This runs on whichever thread destroys the processor
    // (normally the message thread), never the audio thread.
    stopThread (5000);
}

void ContentLoadWorker::post (Kind kind, int slot, PostedJob job)
{
    jassert (slot >= 0 && slot < maxSlots);
    const auto k = (size_t) kind;
    const auto s = (size_t) slot;

    std::function<void()> discardedFromLastRound;
    {
        const juce::ScopedLock sl (lock);
        auto& mb = mailboxes[k][s];

        // A job was already queued for this key and never got to run --
        // let the caller reconcile its own bookkeeping for it before we
        // overwrite it.
        if (mb.hasPending)
            discardedFromLastRound = std::move (mb.pending.onDiscarded);

        mb.pending = std::move (job);
        mb.hasPending = true;

        // A job for this key is actively running right now: it won't see
        // this new request via the mailbox (it already left it), so tell it
        // directly to bail.
        if (mb.runningToken != nullptr)
            mb.runningToken->cancel();
    }

    if (discardedFromLastRound)
        discardedFromLastRound();

    notify();
}

void ContentLoadWorker::run()
{
    while (! threadShouldExit())
    {
        Job job;
        std::shared_ptr<CancelToken> token;
        int foundKind = -1;
        int foundSlot = -1;

        {
            const juce::ScopedLock sl (lock);
            for (int k = 0; k < numKinds && foundKind < 0; ++k)
            {
                for (int s = 0; s < maxSlots; ++s)
                {
                    if (mailboxes[(size_t) k][(size_t) s].hasPending)
                    {
                        foundKind = k;
                        foundSlot = s;
                        break;
                    }
                }
            }

            if (foundKind >= 0)
            {
                auto& mb = mailboxes[(size_t) foundKind][(size_t) foundSlot];
                job = std::move (mb.pending.run);
                mb.pending = {};
                mb.hasPending = false;
                token = std::make_shared<CancelToken>();
                mb.runningToken = token;
            }
        }

        if (foundKind < 0)
        {
            // Bounded wait so a shutdown request (threadShouldExit(), set
            // without necessarily calling notify() from every path) is
            // rechecked promptly even in the rare case nothing wakes us.
            wait (100);
            continue;
        }

        startedCount.fetch_add (1, std::memory_order_relaxed);
        job (*token);
        completedCount.fetch_add (1, std::memory_order_relaxed);

        {
            const juce::ScopedLock sl (lock);
            auto& mb = mailboxes[(size_t) foundKind][(size_t) foundSlot];
            if (mb.runningToken == token)
                mb.runningToken.reset();
        }
    }
}

} // namespace spa::dsp
