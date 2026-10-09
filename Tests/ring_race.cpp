// Thread-race harness for the analyzer ring: one audio-like writer, one UI-like reader.
//   g++ -std=c++17 -O1 -g -fsanitize=thread -pthread Tests/ring_race.cpp -o ring_race && ./ring_race            (new ring)
//   g++ -std=c++17 -O1 -g -fsanitize=thread -pthread -DLEGACY Tests/ring_race.cpp -o ring_legacy && ./ring_legacy  (original design)
#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

#ifdef LEGACY
// The original design: plain float arrays written by the audio thread and read by the UI thread.
struct Ring
{
    static constexpr int size = 1 << 14;
    std::array<float, size> pre {}, post {};
    std::atomic<int> pos { 0 };
    int head = 0;
    void push (float a, float b) { pre[(size_t) head] = a; post[(size_t) head] = b; head = (head + 1) & (size - 1); }
    void publish() { pos.store (head, std::memory_order_release); }
    int readPos() const { return pos.load (std::memory_order_acquire); }
    float readPre (int i) const { return pre[(size_t) i]; }
    float readPost (int i) const { return post[(size_t) i]; }
};
#else
#include "../Source/SpectrumRing.h"
struct Ring : SpectrumRing
{
    float readPre (int i) const { return pre[(size_t) i].load (std::memory_order_relaxed); }
    float readPost (int i) const { return post[(size_t) i].load (std::memory_order_relaxed); }
};
#endif

int main()
{
    static Ring ring;
    std::atomic<bool> done { false };
    double sink = 0;

    std::thread writer ([&]
    {
        for (int block = 0; block < 60000; ++block)
        {
            for (int i = 0; i < 32; ++i) ring.push ((float) block, (float) -block);
            ring.publish();
        }
        done = true;
    });

    std::thread reader ([&]
    {
        int snapshots = 0;
        while (! done.load())
        {
            const int wp = ring.readPos();
            for (int i = 0; i < Ring::size; ++i) sink += ring.readPre ((wp + i) & (Ring::size - 1)) + ring.readPost (i);
            ++snapshots;
        }
        std::printf ("reader took %d full snapshots while the writer ran\n", snapshots);
    });

    writer.join(); reader.join();
    std::printf ("finished (sink %g)\n", sink);
    return 0;
}
