#pragma once
// Lock-free single-writer (audio thread) / single-reader (UI thread) sample ring for the analyzer.
// Every element is a relaxed std::atomic<float>, so concurrent reads never constitute a data race.
// The UI treats the data as approximate (it may see a few newest samples mid-update).
#include <array>
#include <atomic>

struct SpectrumRing
{
    static constexpr int order = 14;
    static constexpr int size  = 1 << order;

    std::array<std::atomic<float>, size> pre, post;
    std::atomic<int> pos { 0 };

    SpectrumRing() { clear(); }

    void clear()
    {
        for (auto& v : pre)  v.store (0.f, std::memory_order_relaxed);
        for (auto& v : post) v.store (0.f, std::memory_order_relaxed);
        pos.store (0, std::memory_order_relaxed);
        head = 0;
    }

    // --- audio thread only ---
    void push (float a, float b)
    {
        pre[(size_t) head].store (a, std::memory_order_relaxed);
        post[(size_t) head].store (b, std::memory_order_relaxed);
        head = (head + 1) & (size - 1);
    }
    void publish() { pos.store (head, std::memory_order_release); }

    // --- UI thread only ---
    int readPos() const { return pos.load (std::memory_order_acquire); }

private:
    int head = 0;   // owned by the audio thread
};
