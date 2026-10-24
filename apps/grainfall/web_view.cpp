#include "web_view.h"

#include <cmath>

namespace web_view
{
namespace
{
using weblink::Kind;

const char *const kSlotNames[] = {
    "Mosaic A",    "Mosaic B",    "Mosaic C",    "Mosaic D",    "Seq A",      "Seq B",
    "Seq C",       "Seq D",       "Glide A",     "Glide B",     "Glide C",    "Glide D",
    "Haze A",      "Haze B",      "Haze C",      "Haze D",      "Tunnel A",   "Tunnel B",
    "Tunnel C",    "Tunnel D",    "Strum A",     "Strum B",     "Strum C",    "Strum D",
    "Blocks A",    "Blocks B",    "Blocks C",    "Blocks D",    "Interrupt A", "Interrupt B",
    "Interrupt C", "Interrupt D", "Arp A",       "Arp B",       "Arp C",      "Arp D",
    "Pattern A",   "Pattern B",   "Pattern C",   "Pattern D",   "Warp A",     "Warp B",
    "Warp C",      "Warp D",
};
static_assert(sizeof(kSlotNames) / sizeof(kSlotNames[0])
                  == GrainEngine::kNumEffects * GrainEngine::kNumVariations,
              "one name per effect slot");

const char *const kShapeNames[] = {"Square", "Ramp up", "Triangle", "Ramp down"};
const char *const kTimeNames[]  = {"1/4", "1/2", "TAP", "2x", "4x", "8x"};
const char *const kGroupNames[] = {"Effect", "Page 1", "Page 2", "Page 3", "Page 4",
                                   "Page 5", "Tempo",  "Switches"};
const char *const kMeterNames[] = {"In", "Wet", "Out"};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}
} // namespace

const weblink::Param kParams[kNumParams] = {
    {"Effect", Kind::kChoice, 0, kSlotNames, Count(kSlotNames), 4},
    {"Activity", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Repeats", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Shape", Kind::kChoice, 2, kShapeNames, Count(kShapeNames), 2},
    {"Time", Kind::kChoice, 2, kTimeNames, Count(kTimeNames), 3},
    {"Filter", Kind::kContinuous, 3, nullptr, 0, 0},
    {"Mix", Kind::kContinuous, 3, nullptr, 0, 0},
    {"Mod depth", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Mod rate", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Resonance", Kind::kContinuous, 5, nullptr, 0, 0},
    {"Volume", Kind::kContinuous, 5, nullptr, 0, 0},
    {"Tempo", Kind::kContinuous, 6, nullptr, 0, 0},
    {"Tap", Kind::kTrigger, 6, nullptr, 0, 0},
    {"Reverse", Kind::kToggle, 7, nullptr, 0, 0},
    {"Hold", Kind::kToggle, 7, nullptr, 0, 0},
    {"Bypass", Kind::kToggle, 7, nullptr, 0, 0},
    {"Mono in", Kind::kToggle, 7, nullptr, 0, 0},
};

void Snapshot(const GrainEngine &engine, Telemetry *t)
{
    t->num_heads        = engine.Heads(t->heads, GrainEngine::kMaxHeads);
    const OnsetLog &log = engine.Onsets();
    t->num_onsets       = log.Count();
    for(int i = 0; i < t->num_onsets; i++)
        t->onsets[i] = log.Get(i);
    t->write_pos     = engine.WritePos();
    t->buffer_frames = engine.BufferFrames();
}

void WriteHello(weblink::Writer &w,
                float            sample_rate,
                uint32_t         buffer_frames,
                size_t           overview_columns)
{
    weblink::Hello h = {};
    h.app              = "grainfall";
    h.sample_rate      = (uint32_t)sample_rate;
    h.buffer_frames    = buffer_frames;
    h.overview_columns = (uint32_t)overview_columns;
    h.groups           = kGroupNames;
    h.num_groups       = Count(kGroupNames);
    h.meters           = kMeterNames;
    h.num_meters       = Count(kMeterNames);
    h.num_params       = kNumParams;
    weblink::WriteHello(w, h);
}

void WriteFrame(weblink::Writer &w,
                const Telemetry &t,
                float            sample_rate,
                float            cpu_avg,
                float            cpu_max,
                const uint8_t   *overview,
                size_t           overview_columns,
                bool             restart,
                OverviewCursor  *cursor)
{
    using weblink::Section;
    uint32_t frame_mask = t.buffer_frames - 1;
    w.Begin(weblink::Msg::kFrame);

    w.U7((uint32_t)Section::kTempo);
    w.U14((uint32_t)(600.0f * sample_rate / t.samples_per_beat + 0.5f));
    w.U21((uint32_t)(t.beat * 1000.0f));
    w.U7((uint32_t)(t.steps_per_beat * 4.0f));
    w.U7(t.following);

    w.U7((uint32_t)Section::kLevels);
    w.U7(3);
    for(float peak : t.peaks)
        w.U14((uint32_t)(fminf(peak, 1.0f) * 16383.0f));

    w.U7((uint32_t)Section::kCpu);
    w.U7((uint32_t)(cpu_avg * 100.0f + 0.5f));
    w.U7((uint32_t)(cpu_max * 100.0f + 0.5f));

    w.U7((uint32_t)Section::kBuffer);
    w.U21(t.write_pos & frame_mask);
    w.U7(t.num_onsets);
    for(int i = 0; i < t.num_onsets; i++)
        w.U21(t.onsets[i] & frame_mask);
    w.U7(t.num_heads);
    for(int i = 0; i < t.num_heads; i++)
    {
        weblink::Head h = t.heads[i];
        h.start &= frame_mask;
        h.pos &= frame_mask;
        weblink::WriteHead(w, h);
    }

    // The overview columns finished since the last frame (all of them, a
    // slice per frame, after a restart).
    uint32_t cols    = (uint32_t)overview_columns;
    uint32_t current = (t.write_pos & frame_mask) / (t.buffer_frames / cols);
    if(restart)
        cursor->next = (current + 1) % cols;
    uint32_t pending = (current - cursor->next) & (cols - 1);
    uint32_t count   = pending < 127 ? pending : 127;
    if(count > 0)
    {
        w.U7((uint32_t)Section::kOverview);
        w.U14(cursor->next);
        w.U7(count);
        for(uint32_t i = 0; i < count; i++)
            w.U7(overview[(cursor->next + i) & (cols - 1)]);
        cursor->next = (cursor->next + count) & (cols - 1);
    }
}
} // namespace web_view
