#include "fx_cloud.h"

#include <cmath>

namespace
{
// Playback speeds per variation; zero ends the list.
const float kRates[4][3] = {
    {1.0f, 0.0f, 0.0f},
    {1.0f, 2.0f, 0.0f},
    {1.0f, 0.5f, 0.0f},
    {0.5f, 1.0f, 2.0f},
};
} // namespace

void FxCloud::Block(const EffectContext &ctx)
{
    float density = 2.0f * powf(30.0f, ctx.activity);
    float size_s  = 0.02f * powf(50.0f, ctx.repeats);
    float size    = size_s * ctx.sample_rate;

    // Overlapping grains are uncorrelated, so scale by the square root.
    float overlap = size_s * (density + ctx.sample_rate / ctx.step_frames);
    float gain    = overlap > 1.0f ? 1.0f / sqrtf(overlap) : 1.0f;

    if(ctx.step)
        Spawn(ctx, size, gain);
    // Jittered so the grains between steps don't fall into a rhythm of
    // their own.
    acc_ += density * ctx.block_frames / ctx.sample_rate * (0.5f + ctx.rng->Uni());
    if(acc_ >= 1.0f)
    {
        acc_ -= 1.0f;
        Spawn(ctx, size, gain);
    }
}

void FxCloud::Spawn(const EffectContext &ctx, float size_frames, float gain)
{
    const float *rates = kRates[ctx.variation & 3];
    int          n     = 1;
    while(n < 3 && rates[n] > 0.0f)
        n++;
    float rate = rates[(int)(ctx.rng->Uni() * n) % n];

    // Up to one step back, but never past the end of the buffer.
    float length = size_frames * rate;
    float reach  = fminf(ctx.step_frames, ctx.buffer_frames * 0.5f);

    VoiceParams p;
    p.length  = length;
    p.start   = ctx.SliceStart(length, ctx.rng->Uni() * reach);
    p.rate    = rate;
    p.reverse = ctx.reverse;
    p.env     = ctx.shape;
    p.gain    = gain;
    p.pan     = 0.5f + (ctx.rng->Uni() - 0.5f) * 0.6f;
    ctx.voices->Start(p);
}
