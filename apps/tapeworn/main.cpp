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
//   Page 4 (LED 2 cyan)    Knob 1 - noise: hiss and hum
//                          Knob 2 - stereo spread: channels fail independently
//   Encoder turn       - tape model (LED 2 flashes its colour; white = off)
//   Encoder long press - AUX type: stop, fail, filter (LED 1 flashes it)
//   Button 1           - bypass (LED 1: green = active, red = bypassed)
//   Button 1 hold 1 s  - mono in: the left input feeds both channels
//                        (LED 1 flashes cyan for on, dark for off)
//   Button 2           - AUX on/off; hold for momentary. LED 1 shows the AUX
//                        colour while on: blue = stop, orange = fail,
//                        white = filter

#include "daisy_pod.h"
#include "tape_engine.h"

using namespace daisy;

constexpr size_t kBufferLen = 1 << 17; // 2.7 s: room for the tape stop
float DSY_SDRAM_BSS buffer_l[kBufferLen];
float DSY_SDRAM_BSS buffer_r[kBufferLen];

DaisyPod   hw;
TapeEngine tape_engine;

constexpr int kNumPages = 4;
constexpr int kNumKnobs = 2;

// Raw knob positions (0..1) per page, initial values chosen to sound sensible.
float page_values[kNumPages][kNumKnobs] = {
    {0.3f, 0.3f},  // wow, flutter
    {0.35f, 0.15f}, // saturate, failure
    {0.0f, 0.5f},  // dry none, volume unity
    {0.2f, 0.0f},  // noise, spread
};
// The knob reading when the page was entered; a knob takes over once it
// moves away from this.
float knob_at_entry[kNumKnobs];
bool  knob_live[kNumKnobs];

int             page     = 0;
int             model    = 5; // VHS 2nd gen
TapeEngine::Aux aux_type = TapeEngine::Aux::kStop;
bool            aux_on   = false;
bool            bypass   = false;
bool            mono_in  = false;

constexpr uint32_t kLongPressMs  = 600;  // encoder: AUX type
constexpr uint32_t kMonoHoldMs   = 1000; // button 1: mono in
constexpr uint32_t kMomentaryMs  = 400;  // button 2: held this long = momentary
constexpr float    kBypassFadeS  = 0.005f;

bool     long_press_done = false; // the current encoder hold changed AUX type
bool     mono_hold_done  = false; // the current button 1 hold toggled mono in
uint32_t aux_held_ms     = 0;     // how long button 2 has been held
float    bypass_mix      = 0.0f;  // 0 = effect, 1 = bypassed
float    bypass_inc      = 0.0f;

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
    if(hw.button1.Pressed() && hw.button1.TimeHeldMs() >= kMonoHoldMs
       && !mono_hold_done)
    {
        mono_in          = !mono_in;
        mono_hold_done   = true;
        mono_flash_until = now_ms + 500;
    }
    if(hw.button1.FallingEdge() && mono_hold_done)
        mono_hold_done = false;
    else if(hw.button1.FallingEdge())
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
    if(hw.encoder.Pressed() && hw.encoder.TimeHeldMs() >= kLongPressMs
       && !long_press_done)
    {
        aux_type = (TapeEngine::Aux)(((int)aux_type + 1) % TapeEngine::kNumAux);
        long_press_done = true;
        aux_flash_until = now_ms + 600;
    }
    if(hw.encoder.FallingEdge() && long_press_done)
        long_press_done = false;
    else if(hw.encoder.FallingEdge())
    {
        page = (page + 1) % kNumPages;
        for(int k = 0; k < kNumKnobs; k++)
        {
            knob_at_entry[k] = knob[k];
            knob_live[k]     = false;
        }
    }
    for(int k = 0; k < kNumKnobs; k++)
    {
        if(!knob_live[k] && fabsf(knob[k] - knob_at_entry[k]) > 0.02f)
            knob_live[k] = true;
        if(knob_live[k])
            page_values[page][k] = knob[k];
    }

    TapeEngine::Params p;
    p.wow      = page_values[0][0];
    p.flutter  = page_values[0][1];
    p.saturate = page_values[1][0];
    p.failure  = page_values[1][1];
    p.noise    = page_values[3][0];
    p.spread   = page_values[3][1];
    p.model    = model;
    p.aux      = aux_type;
    p.aux_on   = aux_on;
    tape_engine.SetParams(p);

    float dry    = page_values[2][0] * page_values[2][0];
    float volume = page_values[2][1] * 2.0f;

    for(size_t i = 0; i < size; i++)
    {
        float in_l = in[0][i];
        float in_r = mono_in ? in_l : in[1][i];

        // Keep the tape running in bypass so it's up to date when you return.
        float wet_l, wet_r;
        tape_engine.Process(in_l, in_r, &wet_l, &wet_r);

        bypass_mix += bypass ? bypass_inc : -bypass_inc;
        bypass_mix = bypass_mix < 0.0f ? 0.0f : (bypass_mix > 1.0f ? 1.0f : bypass_mix);

        float fx_l = wet_l * volume + in_l * dry;
        float fx_r = wet_r * volume + in_r * dry;
        out[0][i]  = fx_l + (in_l - fx_l) * bypass_mix;
        out[1][i]  = fx_r + (in_r - fx_r) * bypass_mix;
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
    bypass_inc = 1.0f / (kBypassFadeS * hw.AudioSampleRate());

    // Every knob starts "not live" so the defaults above hold until a knob moves.
    hw.StartAdc();
    System::Delay(10); // let the ADC settle before taking the reference reading
    hw.ProcessAnalogControls();
    knob_at_entry[0] = hw.GetKnobValue(DaisyPod::KNOB_1);
    knob_at_entry[1] = hw.GetKnobValue(DaisyPod::KNOB_2);

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
            float r = 1.0f, g = 1.0f, b = 1.0f; // model 0, off: white
            if(model > 0)
                Hue(0.8f * (model - 1) / (TapeModel::kNumModels - 2), &r, &g, &b);
            hw.led2.Set(r, g, b);
        }
        else
        {
            const float *c = page_colours[page];
            hw.led2.Set(c[0], c[1], c[2]);
        }
        hw.UpdateLeds();
        System::Delay(20);
    }
}
