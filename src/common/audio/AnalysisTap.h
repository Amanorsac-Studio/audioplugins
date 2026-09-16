#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <vector>

namespace amanorsac
{
/** Carries audio from the audio thread to the window for the analysers.

    One writer (the audio thread) and one reader (the window timer), through
    lock-free FIFOs sized once. When the window is closed nobody reads, the
    FIFOs fill, and the writer simply drops what does not fit: the audio path
    never waits, allocates or locks.
*/
class AnalysisTap
{
public:
    static constexpr int capacity = 1 << 15;

    AnalysisTap()
    {
        for (auto* ring : { &pre, &post })
        {
            ring->left.assign(static_cast<size_t>(capacity), 0.0f);
            ring->right.assign(static_cast<size_t>(capacity), 0.0f);
        }
    }

    /** Audio thread: the signal before processing. */
    void pushPre(const float* left, const float* right, int count) noexcept { push(pre, left, right, count); }
    /** Audio thread: the signal after processing. */
    void pushPost(const float* left, const float* right, int count) noexcept { push(post, left, right, count); }

    /** Window: takes up to `maximum` stereo frames. Returns how many. */
    int pullPre(float* left, float* right, int maximum) noexcept { return pull(pre, left, right, maximum); }
    int pullPost(float* left, float* right, int maximum) noexcept { return pull(post, left, right, maximum); }

    std::atomic<double> sampleRate { 48000.0 };

private:
    struct Ring
    {
        juce::AbstractFifo fifo { capacity };
        std::vector<float> left, right;
    };

    static void push(Ring& ring, const float* left, const float* right, int count) noexcept
    {
        if (left == nullptr || count <= 0) return;
        const auto* r = right != nullptr ? right : left;
        count = juce::jmin(count, ring.fifo.getFreeSpace());
        if (count <= 0) return;
        const auto scope = ring.fifo.write(count);
        auto copy = [&](int start, int size, int offset)
        {
            if (size <= 0) return;
            std::copy_n(left + offset, size, ring.left.begin() + start);
            std::copy_n(r + offset, size, ring.right.begin() + start);
        };
        copy(scope.startIndex1, scope.blockSize1, 0);
        copy(scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    static int pull(Ring& ring, float* left, float* right, int maximum) noexcept
    {
        const auto count = juce::jmin(maximum, ring.fifo.getNumReady());
        if (count <= 0) return 0;
        const auto scope = ring.fifo.read(count);
        auto copy = [&](int start, int size, int offset)
        {
            if (size <= 0) return;
            std::copy_n(ring.left.begin() + start, size, left + offset);
            std::copy_n(ring.right.begin() + start, size, right + offset);
        };
        copy(scope.startIndex1, scope.blockSize1, 0);
        copy(scope.startIndex2, scope.blockSize2, scope.blockSize1);
        return count;
    }

    Ring pre, post;
};
}
