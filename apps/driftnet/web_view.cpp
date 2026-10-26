#include "web_view.h"

#include <cmath>

namespace web_view
{
namespace
{
using weblink::Kind;

const char *const kWetModes[]   = {"Reverb", "Delay", "Slip"};
const char *const kLoopModes[]  = {"Env", "Tape", "Stretch"};
const char *const kRoutings[]   = {"In", "In + loop", "Loop"};
const char *const kSyncs[]      = {"Loop to wet", "Off", "Wet to loop"};
const char *const kSolos[]      = {"Wet", "Both", "Loop"};
const char *const kLengths[]    = {"Half", "Full"};
const char *const kSweeps[]     = {"Bottom", "Top"};
const char *const kPolarities[] = {"Forward", "Reverse"};
const char *const kOutputs[]    = {"Open", "On/off", "ADSR"};
const char *const kOctaves[]    = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
const char *const kDivisions[]  = {"1/32", "1/16", "1/8T", "1/8", "1/8.", "1/4", "1/2", "1"};

const char *const kGroupNames[] = {"Page 1",       "Page 2",  "Page 3", "Channels",
                                   "Hidden options", "DIP switches", "Ramping", "Tempo",
                                   "Synth Mode"};
const char *const kMeterNames[] = {"In", "Wet", "Loop", "Out"};

const weblink::Trace kTraces[kNumTraces] = {
    {"Clock", "kHz", 0, 64},
    {"Input", "dB", -60, 0},
    {"Env threshold", "dB", -60, 0},
    {"Ramp", "%", 0, 100},
};

template <typename T, size_t N>
constexpr uint8_t Count(const T (&)[N])
{
    return (uint8_t)N;
}

float Db(float gain)
{
    return 20.0f * log10f(gain > 1e-4f ? gain : 1e-4f);
}
} // namespace

const weblink::Param kParams[kNumParams] = {
    {"Time", Kind::kContinuous, 0, nullptr, 0, 0},
    {"Wet modify", Kind::kContinuous, 0, nullptr, 0, 0},
    {"Length", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Loop modify", Kind::kContinuous, 1, nullptr, 0, 0},
    {"Clock", Kind::kContinuous, 2, nullptr, 0, 0},
    {"Mix", Kind::kContinuous, 2, nullptr, 0, 0},
    {"Wet mode", Kind::kChoice, 0, kWetModes, Count(kWetModes), 3},
    {"Loop mode", Kind::kChoice, 1, kLoopModes, Count(kLoopModes), 3},
    {"Routing", Kind::kChoice, 2, kRoutings, Count(kRoutings), 3},
    {"Wet on", Kind::kToggle, 3, nullptr, 0, 0},
    {"Freeze", Kind::kToggle, 3, nullptr, 0, 0},
    {"Looper", Kind::kToggle, 3, nullptr, 0, 0},
    {"Overdub", Kind::kToggle, 3, nullptr, 0, 0},
    {"True bypass", Kind::kToggle, 3, nullptr, 0, 0},
    {"Tone", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Stereo width", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Direct loop", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Fade", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Level balance", Kind::kContinuous, 4, nullptr, 0, 0},
    {"Sync", Kind::kChoice, 4, kSyncs, Count(kSyncs), 3},
    {"Spread solo", Kind::kChoice, 4, kSolos, Count(kSolos), 3},
    {"Loop length", Kind::kChoice, 4, kLengths, Count(kLengths), 2},
    {"Classic", Kind::kToggle, 5, nullptr, 0, 0},
    {"MISO", Kind::kToggle, 5, nullptr, 0, 0},
    {"Spread", Kind::kToggle, 5, nullptr, 0, 0},
    {"Dry kill", Kind::kToggle, 5, nullptr, 0, 0},
    {"Trails", Kind::kToggle, 5, nullptr, 0, 0},
    {"Latch", Kind::kToggle, 5, nullptr, 0, 0},
    {"No dub", Kind::kToggle, 5, nullptr, 0, 0},
    {"Smooth", Kind::kToggle, 5, nullptr, 0, 0},
    {"Ramp time", Kind::kToggle, 6, nullptr, 0, 0},
    {"Ramp wet modify", Kind::kToggle, 6, nullptr, 0, 0},
    {"Ramp clock", Kind::kToggle, 6, nullptr, 0, 0},
    {"Ramp loop modify", Kind::kToggle, 6, nullptr, 0, 0},
    {"Ramp length", Kind::kToggle, 6, nullptr, 0, 0},
    {"Bounce", Kind::kToggle, 6, nullptr, 0, 0},
    {"Sweep", Kind::kChoice, 6, kSweeps, Count(kSweeps), 2},
    {"Polarity", Kind::kChoice, 6, kPolarities, Count(kPolarities), 2},
    {"Ramp speed", Kind::kContinuous, 6, nullptr, 0, 0},
    {"Waveform", Kind::kContinuous, 6, nullptr, 0, 0},
    {"Ramping", Kind::kToggle, 6, nullptr, 0, 0},
    {"Expression", Kind::kContinuous, 6, nullptr, 0, 0},
    {"Expression on", Kind::kToggle, 6, nullptr, 0, 0},
    {"Tap", Kind::kTrigger, 7, nullptr, 0, 0},
    {"Tempo", Kind::kContinuous, 7, nullptr, 0, 0},
    {"Follow MIDI clock", Kind::kToggle, 7, nullptr, 0, 0},
    {"Synced", Kind::kToggle, 7, nullptr, 0, 0},
    {"Time division", Kind::kChoice, 7, kDivisions, Count(kDivisions), 4},
    {"Length division", Kind::kChoice, 7, kDivisions, Count(kDivisions), 4},
    {"Synth", Kind::kToggle, 8, nullptr, 0, 0},
    {"Output", Kind::kChoice, 8, kOutputs, Count(kOutputs), 3},
    {"Octave", Kind::kChoice, 8, kOctaves, Count(kOctaves), 5},
    {"Attack", Kind::kContinuous, 8, nullptr, 0, 0},
    {"Decay", Kind::kContinuous, 8, nullptr, 0, 0},
    {"Sustain", Kind::kContinuous, 8, nullptr, 0, 0},
    {"Release", Kind::kContinuous, 8, nullptr, 0, 0},
    {"Portamento", Kind::kContinuous, 8, nullptr, 0, 0},
};

void WriteHello(weblink::Writer &w, float sample_rate, uint32_t ring_frames)
{
    weblink::Hello h  = {};
    h.app             = "driftnet";
    h.sample_rate     = (uint32_t)sample_rate;
    h.buffer_frames   = ring_frames;
    h.overview_columns = (uint32_t)kOverviewColumns;
    h.groups          = kGroupNames;
    h.num_groups      = Count(kGroupNames);
    h.meters          = kMeterNames;
    h.num_meters      = Count(kMeterNames);
    h.num_params      = kNumParams;
    h.traces          = kTraces;
    h.num_traces      = kNumTraces;
    h.trace_period_ms = kTracePeriodMs;
    weblink::WriteHello(w, h);
}

void WriteFrame(weblink::Writer &w,
                const Telemetry &t,
                float            sample_rate,
                uint32_t         ring_frames,
                float            cpu_avg,
                float            cpu_max,
                const uint8_t   *overview,
                OverviewCursor  *cursor)
{
    using weblink::Section;
    uint32_t mask = ring_frames - 1;
    w.Begin(weblink::Msg::kFrame);

    if(t.synced)
    {
        w.U7((uint32_t)Section::kTempo);
        w.U14((uint32_t)(600.0f * sample_rate / t.samples_per_beat + 0.5f));
        w.U21((uint32_t)(t.beat * 1000.0f));
        w.U7(4); // a step a beat
        w.U7(t.following);
    }

    w.U7((uint32_t)Section::kLevels);
    w.U7(4);
    for(float peak : t.peaks)
        w.U14((uint32_t)(fminf(peak, 1.0f) * 16383.0f));

    w.U7((uint32_t)Section::kCpu);
    w.U7((uint32_t)(cpu_avg * 100.0f + 0.5f));
    w.U7((uint32_t)(cpu_max * 100.0f + 0.5f));

    // The loop: the first head, marked as a tap, is the loop itself (length
    // 0 while listening), carrying the clock as its rate so the page can
    // move the heads on between frames. Then whatever plays it.
    const MicroLooper::View &v = t.loop;
    w.U7((uint32_t)Section::kBuffer);
    w.U21(v.write & mask);
    w.U7(0); // no onsets
    w.U7((uint32_t)(1 + v.num_heads));
    weblink::Head loop = {};
    loop.start         = v.start & mask;
    loop.length        = v.playing ? v.loop_len : 0.0f;
    loop.pos           = v.pos & mask;
    loop.rate          = t.clock_hz / sample_rate;
    loop.tap           = true;
    loop.amp           = 1.0f;
    loop.pan           = 0.5f;
    weblink::WriteHead(w, loop);
    for(int i = 0; i < v.num_heads; i++)
    {
        const MicroLooper::Head &m = v.heads[i];
        weblink::Head            h = {};
        h.start                    = m.start & mask;
        h.length                   = m.length;
        h.pos                      = m.pos & mask;
        h.rate                     = m.rate;
        h.reverse                  = m.reverse;
        h.amp                      = m.amp;
        h.pan                      = v.num_heads > 1 ? (float)i : 0.5f;
        weblink::WriteHead(w, h);
    }

    // The overview, round and round, as many columns as fit.
    w.U7((uint32_t)Section::kOverview);
    w.U14(cursor->next);
    w.U7(127);
    for(uint32_t i = 0; i < 127; i++)
        w.U7(overview[(cursor->next + i) % kOverviewColumns]);
    cursor->next = (uint32_t)((cursor->next + 127) % kOverviewColumns);

    w.U7((uint32_t)Section::kTraces);
    w.U7((uint32_t)t.num_points);
    w.U7(kNumTraces);
    for(int p = 0; p < t.num_points; p++)
    {
        const float *x = t.trace[p];
        weblink::WriteTraceValue(w, kTraces[kTraceClock], x[kTraceClock] / 1000.0f);
        weblink::WriteTraceValue(w, kTraces[kTraceInput], Db(x[kTraceInput]));
        weblink::WriteTraceValue(w, kTraces[kTraceThreshold], Db(x[kTraceThreshold]));
        weblink::WriteTraceValue(w, kTraces[kTraceRamp], 100.0f * x[kTraceRamp]);
    }
}
} // namespace web_view
