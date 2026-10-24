// Grainfall - playback voices
//
// A voice plays one slice of the sample buffer: once as a grain, or round
// and round as a micro loop. Every effect is built by starting voices and
// steering them once per block. The pool is fixed and Process() never
// allocates, so it is safe in the audio callback.

#pragma once

#include <cstdint>

#include "dsp.h"
#include "sample_buffer.h"
#include "web_link.h"

// Amplitude contour over each pass through a slice. The first four are the
// Shape control's settings.
enum class Env
{
    kSquare,
    kRampUp,
    kTriangle,
    kRampDown,
    kHann, // smooth grain window
};

// Mean power of an envelope over a pass (1 = a flat one), for keeping
// overlapping grains at a steady level whatever their shape.
float EnvPower(Env env, float soft);

enum class VoiceFilter
{
    kNone,
    kLowPass,
    kBandPass,
};

struct VoiceParams
{
    uint32_t    start      = 0;       // first frame of the slice; must be recorded
    float       length     = 4800.0f; // slice length in buffer frames
    float       rate       = 1.0f;    // playback speed, > 0 (2 = up an octave)
    bool        reverse    = false;   // play the slice end to start
    int         passes     = 1;       // times through the slice; 0 = until released
    float       pass_gain  = 1.0f;    // level multiplier per pass, for decays
    Env         env        = Env::kHann;
    float       soft       = 0.0f; // edge fades of at least this fraction of a pass
    float       gain       = 1.0f;
    float       pan        = 0.5f; // 0 = left, 1 = right
    VoiceFilter filter     = VoiceFilter::kNone;
    float       cutoff     = 1000.0f; // Hz
    float       q          = 0.707f;
    int         crush_bits = 0; // 0 = off, else quantise to this many bits
    int         crush_hold = 1; // keep each sample this many times (decimate)
    uint8_t     tag        = 0; // for the effect's own bookkeeping
};

class VoicePool
{
  public:
    static constexpr int kMaxVoices = 16;

    // Refers to one voice for as long as it plays; 0 is never valid. Calls
    // with a handle whose voice has finished do nothing.
    using Handle = uint32_t;

    void Init(float sample_rate);

    // Starts a voice. If every voice is busy the oldest starts to fade out
    // and this returns 0, so the next start finds room.
    Handle Start(const VoiceParams &p);

    bool Active(Handle h) const;
    void SetRate(Handle h, float rate);
    void SetLength(Handle h, float length); // takes effect on the next pass
    void SetPassGain(Handle h, float pass_gain);
    void SetGain(Handle h, float gain);
    void SetFilter(Handle h, float cutoff, float q);
    void Release(Handle h); // fade out over a few milliseconds
    void ReleaseTag(uint8_t tag);
    void ReleaseAll();
    int  NumActive() const;

    // Where the playing voices are reading, for the web view: up to `max`
    // into `out`, returning how many.
    int Heads(weblink::Head *out, int max) const;

    // Sums every voice reading from buf.
    void Process(const SampleBuffer &buf, float *out_l, float *out_r);

  private:
    struct Voice
    {
        bool     active    = false;
        bool     releasing = false;
        uint16_t serial    = 0;
        uint32_t order     = 0; // start order, to find the oldest

        uint32_t start = 0;
        float    length = 1.0f, inv_length = 1.0f;
        float    offset = 0.0f; // frames into the current pass
        float    rate   = 1.0f;
        bool     reverse = false;
        int      passes = 1, pass = 0;
        float    gain = 1.0f;
        float    level = 1.0f, pass_gain = 1.0f; // level drops by pass_gain each pass
        Env      env = Env::kHann;
        float    soft = 0.0f;
        float    next_length = 0.0f; // > 0: length to switch to at the next pass
        float    inv_fade = 1.0f; // 1 / anti-click fade, as a fraction of a pass
        float    pan_l = 1.0f, pan_r = 1.0f;
        float    release = 1.0f;
        uint8_t  tag = 0;

        VoiceFilter filter = VoiceFilter::kNone;
        dsp::Svf    svf_l, svf_r;

        int   crush_hold = 1, crush_count = 0;
        float crush_q = 0.0f, held_l = 0.0f, held_r = 0.0f;
    };

    Voice *Find(Handle h);
    void   SetFade(Voice &v);

    float    sample_rate_ = 48000.0f;
    float    fade_frames_ = 144.0f; // anti-click fade, output frames
    float    release_inc_ = 0.0f;
    uint32_t order_       = 0;
    Voice    voices_[kMaxVoices];
};
