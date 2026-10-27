#include "wavetables.h"

#include <cmath>

#include "dsp.h"

namespace
{
constexpr int   kLength = Wavetables::kLength;
constexpr int   kHarmonics = kLength / 2 - 1; // the most a 128-sample wave holds
constexpr float kTwoPi  = dsp::kTwoPi;

// How a table is made. Each family sweeps something from wave 0 to wave
// 60, with the table's own a and b.
enum Family
{
    kResonant,  // a resonant peak climbing the harmonics: a = width, b = 0 saw, 1 square, 2 twin peaks
    kAddRising, // harmonics added one by one: a = 1 all, 2 odd
    kPwm,       // pulse width from 50%: a = 0 pulse, 1 saw minus saw; b = narrowest
    kSync,      // a hard-synced wave's ratio rising: a = shape; b = top ratio
    kFormant,   // vowel formants: a = sequence
    kClip,      // gain into a shaper: a = 0 hard, 1 tanh, 2 fold, 3 fuzz; b = shape
    kFm,        // two-operator FM, index rising: a = carrier ratio, b = modulator ratio
    kBell,      // partials dying away at their own rates: a = set
    kOrgan,     // drawbar registrations: a = set
    kStep,      // one harmonic stepping up over the fundamental: a = 1 all, 2 odd, 3 even
    kComb,      // notches sweeping through a spectrum: a = 0 saw, 1 square
    kEcho,      // echoes within the cycle spreading out: a = how many
    kPhaseDist, // phase distortion: a = 0 saw, 1 resonance
    kQuantTime, // fewer and fewer steps per cycle: a = shape
    kQuantLevel,// fewer and fewer bits: a = shape
    kRandom,    // key waves of random spectra or shapes, crossfaded: a = seed, b = 0 spectra, 1 shapes
    kDrift,     // every harmonic's level wandering
    kTransient, // a sine burst, shorter and higher
    kGlass,     // a pair of high partials climbing
    kMorph,     // crossfade from shape a to shape b
    kBass,      // a few low partials, brightening
    kBrass,     // a spectral slope flattening
    kStrings,   // a saw spectrum with shifting high harmonics
    kDigital,   // byte patterns: a = pattern
};

// Shapes for the families that take one.
enum Shape
{
    kSine,
    kTri,
    kSquare,
    kSaw,
    kPulse25,
};

struct Recipe
{
    Family family;
    float  a, b;
};

const Recipe kRecipes[Wavetables::kTables] = {
    {kResonant, 2, 0},   {kResonant, 2, 1},   {kResonant, 6, 0},   {kResonant, 2, 2},
    {kAddRising, 1, 0},  {kAddRising, 2, 0},  {kPwm, 0, 0.02f},    {kPwm, 1, 0.02f},
    {kPwm, 0, 0.35f},    {kSync, kSaw, 4},    {kSync, kSaw, 10},   {kSync, kSquare, 4},
    {kSync, kSquare, 10},{kSync, kSine, 6},   {kSync, kTri, 6},    {kFormant, 0, 0},
    {kFormant, 1, 0},    {kFormant, 2, 0},    {kFormant, 3, 0},    {kClip, 0, kSine},
    {kClip, 1, kSine},   {kClip, 2, kSine},   {kClip, 2, kTri},    {kClip, 3, kSaw},
    {kClip, 3, kSine},   {kFm, 1, 1},         {kFm, 1, 2},         {kFm, 1, 3},
    {kFm, 2, 1},         {kFm, 3, 2},         {kFm, 1, 7},         {kBell, 0, 0},
    {kBell, 1, 0},       {kBell, 2, 0},       {kBell, 3, 0},       {kOrgan, 0, 0},
    {kOrgan, 1, 0},      {kStep, 1, 0},       {kStep, 2, 0},       {kStep, 3, 0},
    {kComb, 0, 0},       {kComb, 1, 0},       {kEcho, 2, 0},       {kEcho, 4, 0},
    {kPhaseDist, 0, 0},  {kPhaseDist, 1, 0},  {kQuantTime, kSine, 0}, {kQuantTime, kSaw, 0},
    {kQuantLevel, kSine, 0}, {kQuantLevel, kTri, 0}, {kRandom, 1, 0}, {kRandom, 2, 1},
    {kDrift, 0, 0},      {kTransient, 0, 0},  {kGlass, 0, 0},      {kMorph, kSine, kSaw},
    {kMorph, kTri, kSquare}, {kMorph, kSquare, kSaw}, {kMorph, kSine, kPulse25}, {kBass, 0, 0},
    {kBrass, 0, 0},      {kStrings, 0, 0},    {kDigital, 0, 0},    {kDigital, 1, 0},
};

float sine_table[kLength]; // sin(2 pi i / 128): harmonic h of sample i is sine_table[(h * i) & 127]

float Frac(float x)
{
    return x - floorf(x);
}

float ShapeAt(int shape, float x) // x: phase, any real
{
    x = Frac(x);
    switch(shape)
    {
        case kSine: return sinf(kTwoPi * x);
        case kTri: return 1.0f - 4.0f * fabsf(x - 0.5f);
        case kSquare: return x < 0.5f ? 1.0f : -1.0f;
        case kSaw: return 1.0f - 2.0f * x;
        case kPulse25:
        default: return x < 0.25f ? 1.0f : -1.0f;
    }
}

// out = the sum of harmonics 1..kHarmonics at amp[h] (amp[0] unused).
void Additive(const float *amp, float *out)
{
    for(int i = 0; i < kLength; i++)
        out[i] = 0.0f;
    for(int h = 1; h <= kHarmonics; h++)
    {
        float a = amp[h];
        if(a == 0.0f)
            continue;
        for(int i = 0; i < kLength; i++)
            out[i] += a * sine_table[(h * i) & (kLength - 1)];
    }
}

// The spectrum of a saw (all harmonics) or square (odd ones).
float Spectrum(bool odd, int h)
{
    return odd && h % 2 == 0 ? 0.0f : 1.0f / (float)h;
}

// Vowel formants, Hz: F1, F2, F3.
const float kVowels[5][3] = {
    {800, 1150, 2900}, // a
    {400, 1700, 2600}, // e
    {300, 2200, 3000}, // i
    {450, 800, 2830},  // o
    {325, 700, 2530},  // u
};
const int kVowelOrders[4][5] = {
    {0, 1, 2, 3, 4}, // a e i o u
    {4, 3, 2, 1, 0}, // u o i e a
    {0, 1, 2, 3, 4},
    {0, 3, 0, 3, 0}, // a o a o
};

void Formant(int sequence, float t, float *out)
{
    float f0  = sequence == 2 ? 220.0f : 110.0f;
    float pos = t * 4.0f;
    int   k   = (int)pos < 3 ? (int)pos : 3;
    float mix = pos - (float)k;
    const float *v0 = kVowels[kVowelOrders[sequence][k]];
    const float *v1 = kVowels[kVowelOrders[sequence][k + 1]];
    const float gains[3] = {1.0f, 0.5f, 0.25f};
    const float widths[3] = {90.0f, 130.0f, 180.0f}; // Hz
    float amp[kHarmonics + 1] = {};
    for(int h = 1; h <= kHarmonics; h++)
    {
        float f = f0 * (float)h, a = 0.02f / (float)h;
        for(int n = 0; n < 3; n++)
        {
            float d = (f - dsp::Lerp(v0[n], v1[n], mix)) / widths[n];
            a += gains[n] * expf(-d * d);
        }
        amp[h] = a;
    }
    Additive(amp, out);
}

// Partials (harmonic numbers), their levels and how fast each dies away.
struct Partials
{
    int   n;
    int   h[7];
    float level[7];
    float decay[7];
};
const Partials kBells[4] = {
    {7, {1, 2, 3, 5, 7, 9, 12}, {1, 0.8f, 0.6f, 0.5f, 0.4f, 0.3f, 0.25f}, {0.2f, 0.5f, 1, 1.5f, 2, 3, 4}},
    {6, {1, 4, 9, 13, 19, 24}, {1, 0.7f, 0.6f, 0.5f, 0.4f, 0.3f}, {0.3f, 0.6f, 1, 2, 3, 4}},
    {4, {1, 4, 10, 18}, {1, 0.9f, 0.6f, 0.4f}, {0.5f, 2, 4, 6}},
    {3, {1, 4, 10}, {1, 0.5f, 0.2f}, {0.1f, 0.8f, 2}},
};

// Drawbar footages as harmonics of 16': 16', 5 1/3', 8', 4', 2 2/3', 2',
// 1 3/5', 1 1/3', 1'.
const int   kDrawbars[9]       = {1, 3, 2, 4, 6, 8, 10, 12, 16};
const float kRegistrations[2][2][9] = {
    {{8, 8, 8, 0, 0, 0, 0, 0, 0}, {8, 8, 8, 8, 8, 8, 8, 8, 8}},
    {{0, 0, 8, 0, 8, 0, 0, 0, 0}, {8, 0, 8, 8, 0, 0, 0, 0, 8}},
};

void Generate(int table, float t, float *out)
{
    const Recipe &r = kRecipes[table];
    float amp[kHarmonics + 1] = {};
    switch(r.family)
    {
        case kResonant:
        {
            float centre = 1.0f + t * 40.0f;
            for(int h = 1; h <= kHarmonics; h++)
            {
                float d    = ((float)h - centre) / r.a;
                float peak = expf(-d * d);
                if(r.b == 2)
                {
                    float d2 = ((float)h - centre * 1.6f) / r.a;
                    peak += expf(-d2 * d2);
                }
                amp[h] = Spectrum(r.b == 1, h) * (0.15f + 3.0f * peak);
            }
            Additive(amp, out);
            break;
        }
        case kAddRising:
        {
            float n = 1.0f + t * (float)(kHarmonics - 1);
            for(int h = 1; h <= kHarmonics; h++)
                amp[h] = Spectrum(r.a == 2, h) * dsp::Clamp(n - (float)h + 1.0f, 0.0f, 1.0f);
            Additive(amp, out);
            break;
        }
        case kPwm:
        {
            float w = 0.5f - t * (0.5f - r.b);
            for(int i = 0; i < kLength; i++)
            {
                float x = (float)i / kLength;
                out[i]  = r.a == 0 ? (x < w ? 1.0f : -1.0f) : ShapeAt(kSaw, x) - ShapeAt(kSaw, x + w);
            }
            break;
        }
        case kSync:
        {
            float ratio = 1.0f + t * (r.b - 1.0f);
            for(int i = 0; i < kLength; i++)
                out[i] = ShapeAt((int)r.a, Frac((float)i / kLength * ratio));
            break;
        }
        case kFormant: Formant((int)r.a, t, out); break;
        case kClip:
        {
            float gain = 1.0f + t * (r.a == 2 ? 6.0f : 24.0f);
            for(int i = 0; i < kLength; i++)
            {
                float v = gain * ShapeAt((int)r.b, (float)i / kLength);
                switch((int)r.a)
                {
                    case 0: v = dsp::Clamp(v, -1.0f, 1.0f); break;
                    case 1: v = tanhf(v); break;
                    case 2: v = 1.0f - 4.0f * fabsf(Frac((v + 1.0f) * 0.25f) - 0.5f); break;
                    default: v = v > 0.0f ? tanhf(v) : 0.5f * tanhf(0.3f * v); break;
                }
                out[i] = v;
            }
            break;
        }
        case kFm:
        {
            float index = t * 8.0f;
            for(int i = 0; i < kLength; i++)
            {
                float x = (float)i / kLength;
                out[i]  = sinf(kTwoPi * r.a * x + index * sinf(kTwoPi * r.b * x));
            }
            break;
        }
        case kBell:
        {
            const Partials &p = kBells[(int)r.a];
            for(int k = 0; k < p.n; k++)
                amp[p.h[k]] += p.level[k] * expf(-t * p.decay[k] * 6.0f);
            Additive(amp, out);
            break;
        }
        case kOrgan:
        {
            const float(*reg)[9] = kRegistrations[(int)r.a];
            for(int k = 0; k < 9; k++)
                amp[kDrawbars[k]] += dsp::Lerp(reg[0][k], reg[1][k], t) / 8.0f;
            Additive(amp, out);
            break;
        }
        case kStep:
        {
            int step = (int)(t * 20.0f);
            int n    = r.a == 1 ? 2 + (int)(t * 40.0f) : (r.a == 2 ? 3 + 2 * step : 2 + 2 * step);
            amp[1]   = 1.0f;
            amp[n] += 0.8f;
            Additive(amp, out);
            break;
        }
        case kComb:
        {
            float spacing = t * 0.5f;
            for(int h = 1; h <= kHarmonics; h++)
                amp[h] = Spectrum(r.a == 1, h) * fabsf(cosf(dsp::kPi * (float)h * spacing));
            Additive(amp, out);
            break;
        }
        case kEcho:
        {
            int   n     = (int)r.a;
            float delay = t / (float)(n + 1);
            for(int i = 0; i < kLength; i++)
            {
                float x = (float)i / kLength, v = 0.0f, g = 1.0f;
                for(int k = 0; k <= n; k++, g *= 0.6f)
                    v += g * ShapeAt(kSaw, x - (float)k * delay);
                out[i] = v;
            }
            break;
        }
        case kPhaseDist:
        {
            float w = 0.5f - 0.48f * t, ratio = 1.0f + t * 15.0f;
            for(int i = 0; i < kLength; i++)
            {
                float x = (float)i / kLength;
                if(r.a == 0)
                {
                    float y = x < w ? 0.5f * x / w : 0.5f + 0.5f * (x - w) / (1.0f - w);
                    out[i]  = -cosf(kTwoPi * y);
                }
                else
                    out[i] = (1.0f - x) * cosf(kTwoPi * ratio * x);
            }
            break;
        }
        case kQuantTime:
        {
            float steps = fmaxf(2.0f, roundf(64.0f * exp2f(-5.0f * t)));
            for(int i = 0; i < kLength; i++)
            {
                float x = floorf((float)i / kLength * steps) / steps;
                out[i]  = ShapeAt((int)r.a, x + 0.5f / steps);
            }
            break;
        }
        case kQuantLevel:
        {
            float levels = exp2f(7.0f - 6.5f * t);
            for(int i = 0; i < kLength; i++)
                out[i] = roundf(ShapeAt((int)r.a, (float)i / kLength) * levels) / levels;
            break;
        }
        case kRandom:
        {
            // Six key waves, as the Microwave built its tables from key
            // waves, crossfaded in between.
            constexpr int kKeys = 6;
            float         pos   = t * (kKeys - 1);
            int           k     = (int)pos < kKeys - 2 ? (int)pos : kKeys - 2;
            float         mix   = pos - (float)k;
            float         keys[2][kLength];
            for(int j = 0; j < 2; j++)
            {
                dsp::Rng rng((uint32_t)(r.a * 7919.0f) + (uint32_t)(k + j) * 104729u);
                if(r.b == 0)
                {
                    float key_amp[kHarmonics + 1] = {};
                    for(int h = 1; h <= kHarmonics; h++)
                        key_amp[h] = rng.Uni() * rng.Uni() / powf((float)h, 0.7f);
                    Additive(key_amp, keys[j]);
                }
                else
                {
                    // 16 random points, joined smoothly.
                    float pts[16];
                    for(float &p : pts)
                        p = rng.Bi();
                    for(int i = 0; i < kLength; i++)
                    {
                        float x = (float)i / kLength * 16.0f;
                        int   a = (int)x;
                        float f = x - (float)a;
                        f       = f * f * (3.0f - 2.0f * f);
                        keys[j][i] = dsp::Lerp(pts[a], pts[(a + 1) & 15], f);
                    }
                }
            }
            for(int i = 0; i < kLength; i++)
                out[i] = dsp::Lerp(keys[0][i], keys[1][i], mix);
            break;
        }
        case kDrift:
        {
            dsp::Rng rng(31337);
            for(int h = 1; h <= kHarmonics; h++)
            {
                float rate = rng.Range(0.5f, 3.0f), phase = rng.Uni();
                amp[h]     = (0.5f + 0.5f * sinf(kTwoPi * (rate * t + phase))) / sqrtf((float)h);
            }
            Additive(amp, out);
            break;
        }
        case kTransient:
        {
            float freq = 2.0f + t * 28.0f, width = 1.0f - 0.9f * t;
            for(int i = 0; i < kLength; i++)
            {
                float x = (float)i / kLength;
                float w = x < width ? 0.5f - 0.5f * cosf(kTwoPi * x / width) : 0.0f;
                out[i]  = w * sinf(kTwoPi * freq * x);
            }
            break;
        }
        case kGlass:
        {
            int n  = 8 + (int)(t * 40.0f);
            amp[1] = 1.0f;
            amp[n] += 0.7f;
            amp[n + 1] += 0.7f;
            Additive(amp, out);
            break;
        }
        case kMorph:
            for(int i = 0; i < kLength; i++)
            {
                float x = (float)i / kLength;
                out[i]  = dsp::Lerp(ShapeAt((int)r.a, x), ShapeAt((int)r.b, x), t);
            }
            break;
        case kBass:
            for(int h = 1; h <= kHarmonics; h++)
                amp[h] = expf(-(float)(h - 1) / (0.5f + 8.0f * t)) / (float)h;
            Additive(amp, out);
            break;
        case kBrass:
            for(int h = 1; h <= kHarmonics; h++)
                amp[h] = powf((float)h, -(3.0f - 2.4f * t));
            Additive(amp, out);
            break;
        case kStrings:
            for(int h = 1; h <= kHarmonics; h++)
                amp[h] = (1.0f + 0.6f * sinf(1.7f * (float)h + 6.0f * t)) / (float)h;
            Additive(amp, out);
            break;
        case kDigital:
        {
            int k = 1 + (int)(t * 60.0f);
            for(int i = 0; i < kLength; i++)
            {
                int v  = r.a == 0 ? ((i * k) & 127) ^ (i & 0x55)
                                  : ((i * k) ^ (i >> (1 + k % 4)) * 7) & 127;
                out[i] = (float)v / 63.5f - 1.0f;
            }
            break;
        }
    }
}

// DC out, peak at full scale, rounded to 8 bits.
void Store(const float *in, int8_t *out)
{
    float mean = 0.0f;
    for(int i = 0; i < kLength; i++)
        mean += in[i];
    mean /= kLength;
    float peak = 0.0f;
    for(int i = 0; i < kLength; i++)
        peak = fmaxf(peak, fabsf(in[i] - mean));
    float scale = peak > 1e-6f ? 127.0f / peak : 0.0f;
    for(int i = 0; i < kLength; i++)
        out[i] = (int8_t)dsp::Clamp(roundf((in[i] - mean) * scale), -127.0f, 127.0f);
}
} // namespace

const char *const Wavetables::kNames[kTables] = {
    "Reso Saw",   "Reso Sqr",   "Reso Wide",  "Reso Twin",  "Saw Sweep",  "Sqr Sweep",
    "Pulse PWM",  "Saw PWM",    "Micro PWM",  "Sync Saw",   "Sync Saw+",  "Sync Sqr",
    "Sync Sqr+",  "Sync Sine",  "Sync Tri",   "Vowels",     "Vowels R",   "Vowels Hi",
    "Ah Oh",      "Clip Sine",  "Tanh Sine",  "Fold Sine",  "Fold Tri",   "Fuzz Saw",
    "Fuzz Sine",  "FM 1:1",     "FM 1:2",     "FM 1:3",     "FM 2:1",     "FM 3:2",
    "FM 1:7",     "Bell",       "Glass Bell", "Mallet",     "Vibes",      "Organ",
    "Organ Perc", "Harm Step",  "Odd Step",   "Even Step",  "Comb Saw",   "Comb Sqr",
    "Echo 2",     "Echo 4",     "PD Saw",     "PD Reso",    "Step Sine",  "Step Saw",
    "Crush Sine", "Crush Tri",  "Random",     "Random 2",   "Drift",      "Transient",
    "Glass",      "Sine Saw",   "Tri Sqr",    "Sqr Saw",    "Sine Pulse", "Bass",
    "Brass",      "Strings",    "Digital",    "Digital 2",
};

void Wavetables::Init(Wave *memory)
{
    waves_ = memory;
    for(int i = 0; i < kLength; i++)
        sine_table[i] = sinf(kTwoPi * (float)i / kLength);

    float wave[kLength];
    for(int table = 0; table < kTables; table++)
    {
        for(int w = 0; w < kTableWaves; w++)
        {
            Generate(table, (float)w / (kTableWaves - 1), wave);
            Store(wave, waves_[table * kWaves + w]);
        }
        // The last three, in every table.
        const int shapes[3] = {kTri, kSquare, kSaw};
        for(int k = 0; k < 3; k++)
        {
            for(int i = 0; i < kLength; i++)
                wave[i] = ShapeAt(shapes[k], (float)i / kLength);
            Store(wave, waves_[table * kWaves + kTableWaves + k]);
        }
    }
}
