// Tapeworn - lo-fi tape machine for the Daisy Pod (Daisy Seed 3)
//
// Our own take on a deconstructed VHS/cassette deck (after Chase Bliss's
// Generation Loss): every artifact a tape machine adds gets its own control.
// The two knobs are shared across four pages; press the encoder to change
// page (LED 2 shows which). After a page change a knob does nothing until it
// is moved, so values don't jump.
//
//   Page 1 (LED 2 blue)    Knob 1 - wow: slow, wandering pitch drift
//                          Knob 2 - flutter: fast pitch and level quiver
//   Page 2 (LED 2 magenta) Knob 1 - saturate: magnetic hysteresis drive
//                          Knob 2 - failure: crinkles, pops, snags, drops
//   Page 3 (LED 2 yellow)  Knob 1 - dry level (none to unity)
//                          Knob 2 - wet volume (noon is unity, max x2)
//                          (a limiter holds dry + wet under -1 dBFS)
//   Page 4 (LED 2 cyan)    Knob 1 - noise: hiss and hum
//                          Knob 2 - stereo spread: channels fail independently
//   Encoder turn       - tape model (LED 2 flashes its colour; white = flat)
//   Encoder long press - AUX type: stop, fail, filter (LED 1 flashes it)
//   Button 1           - bypass (LED 1: green = active, red = bypassed)
//   Button 1 hold 1 s  - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 2           - AUX on/off; hold for momentary. LED 1 shows the AUX
//                        colour while on: blue = stop, orange = fail,
//                        white = filter

#include "daisy_pod.h"
#include "bypass_fade.h"
#include "limiter.h"
#include "paged_knobs.h"
#include "press_hold.h"
#include "tape_engine.h"

using namespace daisy;

constexpr size_t kBufferLen = 1 << 17; // 2.7 s: room for the tape stop
float DSY_SDRAM_BSS buffer_l[kBufferLen];
float DSY_SDRAM_BSS buffer_r[kBufferLen];

DaisyPod   hw;
TapeEngine tape_engine;
Limiter    limiter; // on dry + wet, so turning both up doesn't clip

constexpr int kNumPages = 4;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
PagedKnobs<kNumPages, kNumKnobs> pages({
    {0.3f, 0.3f},   // wow, flutter
    {0.35f, 0.15f}, // saturate, failure
    {0.0f, 0.5f},   // dry none, volume unity
    {0.2f, 0.0f},   // noise, spread
});

int             model    = 5; // VHS 2nd gen
TapeEngine::Aux aux_type = TapeEngine::Aux::kStop;
bool            aux_on   = false;
bool            bypass   = false;
bool            mono_in  = false;

constexpr uint32_t kMomentaryMs = 400; // button 2: held this long = momentary

PressHold  encoder_press(600);  // hold: AUX type
PressHold  button1_press(1000); // hold: mono in
BypassFade bypass_fade;
uint32_t   aux_held_ms = 0; // how long button 2 has been held

// Read by the main loop for the LEDs: flash until these times (ms).
volatile uint32_t model_flash_until = 0;
volatile uint32_t aux_flash_until   = 0;
volatile uint32_t mono_flash_until  = 0;

void AudioCallback(AudioHandle::InputBuffer  in,
                   AudioHandle::OutputBuffer out,
                   size_t                    size)
{
    hw.ProcessAllControls();
    uint32_t now_ms = System::GetNow();

    // Button 1: a tap toggles bypass on release, a 1 s hold toggles mono in.
    PressHold::Event b1 = button1_press.Update(hw.button1.Pressed(),
                                               hw.button1.TimeHeldMs());
    if(b1 == PressHold::Event::kHold)
    {
        mono_in          = !mono_in;
        mono_flash_until = now_ms + 500;
    }
    else if(b1 == PressHold::Event::kTap)
        bypass = !bypass;

    // Button 2: press toggles AUX; releasing after a hold turns it back off.
    if(hw.button2.RisingEdge())
        aux_on = !aux_on;
    if(hw.button2.Pressed())
        aux_held_ms = hw.button2.TimeHeldMs();
    if(hw.button2.FallingEdge() && aux_held_ms >= kMomentaryMs)
        aux_on = false;

    int inc = hw.encoder.Increment();
    if(inc != 0)
    {
        model += inc;
        model = model < 0 ? 0 : (model >= TapeModel::kNumModels ? TapeModel::kNumModels - 1 : model);
        model_flash_until = now_ms + 1000;
    }

    const DaisyPod::Knob knob_ids[kNumKnobs] = {DaisyPod::KNOB_1, DaisyPod::KNOB_2};
    float                knob[kNumKnobs];
    for(int k = 0; k < kNumKnobs; k++)
        knob[k] = hw.GetKnobValue(knob_ids[k]);

    // Long press cycles the AUX type as soon as the hold is long enough; a
    // short press changes page on release.
    PressHold::Event enc = encoder_press.Update(hw.encoder.Pressed(),
                                                hw.encoder.TimeHeldMs());
    if(enc == PressHold::Event::kHold)
    {
        aux_type = (TapeEngine::Aux)(((int)aux_type + 1) % TapeEngine::kNumAux);
        aux_flash_until = now_ms + 600;
    }
    else if(enc == PressHold::Event::kTap)
        pages.NextPage(knob);
    pages.Update(knob);

    TapeEngine::Params p;
    p.wow      = pages.Value(0, 0);
    p.flutter  = pages.Value(0, 1);
    p.saturate = pages.Value(1, 0);
    p.failure  = pages.Value(1, 1);
    p.noise    = pages.Value(3, 0);
    p.spread   = pages.Value(3, 1);
    p.model    = model;
    p.aux      = aux_type;
    p.aux_on   = aux_on;
    tape_engine.SetParams(p);

    float dry    = pages.Value(2, 0) * pages.Value(2, 0);
    float volume = pages.Value(2, 1) * 2.0f;

    for(size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = mono_in ? in_l : in[1][i];

        // Keep the tape running in bypass so it's up to date when you return.
        float wet_l, wet_r;
        tape_engine.Process(in_l, in_r, &wet_l, &wet_r);

        float fx_l = wet_l * volume + in_l * dry;
        float fx_r = wet_r * volume + in_r * dry;
        limiter.Process(&fx_l, &fx_r);
        bypass_fade.Process(bypass, in_l, in_r, &fx_l, &fx_r);
        out[0][i] = fx_l;
        out[1][i] = fx_r;
    }
}

// Hue 0..1 around the colour wheel, full saturation and value.
void Hue(float h, float *r, float *g, float *b)
{
    float x = h * 6.0f;
    *r      = fminf(fmaxf(fabsf(x - 3.0f) - 1.0f, 0.0f), 1.0f);
    *g      = fminf(fmaxf(2.0f - fabsf(x - 2.0f), 0.0f), 1.0f);
    *b      = fminf(fmaxf(2.0f - fabsf(x - 4.0f), 0.0f), 1.0f);
}

int main(void)
{
    hw.Init();
    hw.SetAudioBlockSize(4);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    tape_engine.Init(hw.AudioSampleRate(), buffer_l, buffer_r, kBufferLen);
    limiter.Init(hw.AudioSampleRate());
    bypass_fade.Init(hw.AudioSampleRate());

    // Every knob starts "not live" so the defaults above hold until a knob moves.
    hw.StartAdc();
    System::Delay(10); // let the ADC settle before taking the reference reading
    hw.ProcessAnalogControls();
    pages.Latch({hw.GetKnobValue(DaisyPod::KNOB_1), hw.GetKnobValue(DaisyPod::KNOB_2)});

    hw.StartAudio(AudioCallback);

    const float page_colours[kNumPages][3] = {
        {0.0f, 0.0f, 1.0f}, // blue
        {1.0f, 0.0f, 1.0f}, // magenta
        {1.0f, 0.7f, 0.0f}, // yellow
        {0.0f, 1.0f, 1.0f}, // cyan
    };
    const float aux_colours[TapeEngine::kNumAux][3] = {
        {0.0f, 0.0f, 1.0f},  // stop: blue
        {1.0f, 0.3f, 0.0f},  // fail: orange
        {1.0f, 1.0f, 1.0f},  // filter: white
    };
    while(1)
    {
        uint32_t    now = System::GetNow();
        const float *a  = aux_colours[(int)aux_type];

        // LED 1: mono in / AUX type flashes, then bypass, AUX, or active.
        if(now < mono_flash_until)
        {
            if(mono_in)
                hw.led1.Set(0.0f, 1.0f, 1.0f);
            else
                hw.led1.Set(0.0f, 0.0f, 0.0f);
        }
        else if(now < aux_flash_until)
        {
            bool lit = (now / 75) % 2;
            hw.led1.Set(lit ? a[0] : 0.0f, lit ? a[1] : 0.0f, lit ? a[2] : 0.0f);
        }
        else if(bypass)
            hw.led1.Set(1.0f, 0.0f, 0.0f);
        else if(aux_on)
            hw.led1.Set(a[0], a[1], a[2]);
        else
            hw.led1.Set(0.0f, 1.0f, 0.0f);

        // LED 2: page colour, or the model's colour just after a change.
        if(now < model_flash_until)
        {
            float r = 1.0f, g = 1.0f, b = 1.0f; // model 0, flat: white
            if(model > 0)
                Hue(0.8f * (model - 1) / (TapeModel::kNumModels - 2), &r, &g, &b);
            hw.led2.Set(r, g, b);
        }
        else
        {
            const float *c = page_colours[pages.Page()];
            hw.led2.Set(c[0], c[1], c[2]);
        }
        hw.UpdateLeds();
        System::Delay(20);
    }
}
