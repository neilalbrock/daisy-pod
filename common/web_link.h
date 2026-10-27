// Web link - SysEx protocol between an app and the web control surface
//
// The page in web/ talks to the Seed over USB MIDI. It sets parameters and
// asks for information with short SysEx commands; while it keeps sending
// keep-alives, the app streams its parameter list, state and a telemetry
// frame (tempo, levels, CPU, buffer and read heads) back as SysEx. Nothing
// is sent when no page is listening, so a DAW never sees it.
//
// Every message is F0 7D 02 <type> <payload> F7: 7D is the manufacturer ID
// set aside for non-commercial use, 02 the protocol version. Payload
// numbers are 7, 14 or 21 bits wide, most significant 7 bits first, and
// strings are 7-bit ASCII ending in 0. web/js/protocol.js is the other half
// and must match.
//
// libDaisy-free: the app reads SysEx from its MIDI handler into Receive()
// (from the audio callback) and sends what the Writer builds (from the main
// loop).

#pragma once

#include <cstddef>
#include <cstdint>

namespace weblink
{
constexpr uint8_t kId      = 0x7D;
constexpr uint8_t kVersion = 2; // 2: 14-bit parameter indices

// libDaisy's USB MIDI transmit buffer holds 768 bytes of SysEx and doesn't
// check, so stay well under it.
constexpr size_t kMaxMessage = 720;

enum class Msg : uint8_t
{
    // App to page
    kHello = 0x01, // a Hello
    kParam = 0x02, // parameters' descriptions, as many as fit
    kState = 0x03, // first (14), count (14), then that run of values (14 each)
    kFrame = 0x04, // telemetry sections, below

    // Page to app
    kRequest      = 0x40, // send everything from the start
    kSet          = 0x41, // index (14), value (14)
    kKeepAlive    = 0x42,
    kPresetSelect = 0x43, // slot (7): load it, or just select it if empty
    kPresetSave   = 0x44, // slot (7): save the current settings there
    kPresetReset  = 0x45, // factory reset: every preset and saved setting
};

enum class Kind : uint8_t
{
    kContinuous, // value 0..16383 for 0..1
    kChoice,     // value is the option index
    kToggle,     // 0 or 1
    kTrigger,    // any value fires it
};

// Telemetry frame sections, each starting with its tag.
enum class Section : uint8_t
{
    kTempo    = 1, // bpm x 10 (14), beat x 1000 (21), steps per beat x 4 (7), clock (7)
    kLevels   = 2, // count (7), then peak since last frame x 16383 (14) each
    kCpu      = 3, // average % (7), peak % (7)
    kBuffer   = 4, // write head (21), onsets: count (7) + frame (21) each,
                   // heads: count (7) + Head each
    kOverview = 5, // first column (14), count (7), sqrt(peak) x 127 (7) each
    kTraces   = 6, // points (7), traces (7), then each point's value per
                   // trace, 0..16383 across the trace's range (14), oldest first
    kXY       = 7, // count (7), then x, y pairs, -1..1 as 0..126 (7 each)
    kPresets  = 8, // slot (7, 127: none yet), modified (7), slots in use (14, a bit each)
};

// Whether a parameter is kept in flash (common/presets.h): in each preset
// (the sound), as a global setting of the app's (mono in, trails...), or
// not at all (on/off, momentary switches, triggers).
enum class Store : uint8_t
{
    kNone,
    kPreset,
    kGlobal,
};

// A value plotted over time, described in the hello.
struct Trace
{
    const char *name;
    const char *unit;
    int16_t     min, max; // the range shown (-8192..8191)
};

// What the page needs to know about the app, sent first.
struct Hello
{
    const char        *app;
    uint32_t           sample_rate;
    uint32_t           buffer_frames;    // 0: no buffer to show
    uint32_t           overview_columns; // 0: no buffer overview
    const char *const *groups; // control groups, by Param::group
    uint8_t            num_groups;
    const char *const *meters; // kLevels, in order
    uint8_t            num_meters;
    uint16_t           num_params;
    const Trace       *traces; // kTraces, in order
    uint8_t            num_traces;
    uint8_t            trace_period_ms; // between points
    const char        *xy;              // the kXY plot's title, or nullptr
    uint8_t            presets;         // preset slots, 0: none
};

// One read head in the buffer, for the kBuffer section.
struct Head
{
    uint32_t start;  // first frame of the slice
    float    length; // slice length, frames (0 for a delay tap)
    uint32_t pos;    // frame being read now
    float    rate;   // playback speed, > 0
    bool     reverse;
    bool     tap; // a delay tap rather than a voice
    float    amp; // 0..1
    float    pan; // 0 = left, 1 = right
};

struct Param
{
    const char        *name;
    Kind               kind;
    uint8_t            group;   // index into the app's group names
    const char *const *options; // kChoice
    uint8_t            num_options;
    uint8_t            columns; // kChoice: lay the options out in this many columns
    Store              store;   // not sent: what common/presets.h keeps (kNone if left out)
};

// Builds one message. The buffer belongs to the caller (it can live in
// SDRAM: the Writer has no constructor that touches it).
class Writer
{
  public:
    void Init(uint8_t *buf) { buf_ = buf; }

    void Begin(Msg type);
    void U7(uint32_t v);
    void U14(uint32_t v);
    void U21(uint32_t v);
    void Unit(float v); // 0..1 as 14 bits
    void Str(const char *s);

    // Appends F7. Returns the length, or 0 if the message overflowed.
    size_t End();

    // Bytes left before kMaxMessage, keeping one for F7.
    size_t Room() const { return n_ + 1 < kMaxMessage ? kMaxMessage - 1 - n_ : 0; }

    // To take back what didn't fit: Mark() before writing it, then, if
    // Overflowed(), Rewind() to the mark.
    size_t Mark() const { return n_; }
    bool   Overflowed() const { return overflow_; }
    void   Rewind(size_t mark)
    {
        n_        = mark;
        overflow_ = false;
    }

    uint8_t *Data() { return buf_; }

  private:
    void Byte(uint8_t b);

    uint8_t *buf_      = nullptr;
    size_t   n_        = 0;
    bool     overflow_ = false;
};

// Standard messages.
void WriteHello(Writer &w, const Hello &h);
void WriteParam(Writer &w, int index, const Param &p); // one description, within a kParam
void WriteHead(Writer &w, const Head &h);

// Values for the kTraces and kXY sections.
void WriteTraceValue(Writer &w, const Trace &t, float v); // clamped to its range
void WriteXY(Writer &w, float x, float y);                // each -1..1

// What a command from the page asks for.
struct Command
{
    Msg      type;
    uint16_t index; // kSet: the parameter; kPresetSelect, kPresetSave: the slot
    int      raw;   // kSet: the 14-bit value
};

// Parses a SysEx payload as libDaisy delivers it (no F0 or F7). False if
// it isn't ours.
bool Parse(const uint8_t *data, int length, Command *out);

// Tracks whether a page is listening and what it still needs. Receive()
// runs in the audio callback, Next() in the main loop: they share only
// plain flags and counters.
class Session
{
  public:
    static constexpr uint32_t kTimeoutMs = 3000;
    static constexpr uint32_t kStateMs   = 500; // resend state at least this often
    static constexpr uint32_t kChangeMs  = 50;  // and changes at most this often
    static constexpr uint32_t kFrameMs   = 30;
    static constexpr int      kStateRun  = 256; // values in one kState message

    void Init(const Param *params, int num_params)
    {
        params_     = params;
        num_params_ = num_params;
    }

    // Handles a request or keep-alive. Returns true, with *command filled,
    // for a kSet or a preset command the app should act on.
    bool Receive(const uint8_t *data, int length, uint32_t now_ms, Command *command);

    enum class Send
    {
        kNothing,
        kHello,
        kParam,
        kState,
        kFrame,
    };

    // The message to send now, if any. state_changed: the app's values
    // differ from the last state sent. For kHello and kFrame the app writes
    // the message; for kParam and kState, WriteParams and WriteState do.
    Send Next(uint32_t now_ms, bool state_changed);

    // The next parameters' descriptions, as many as fit in one message.
    void WriteParams(Writer &w);
    // The next run of values, from raw (every parameter's, as the app reads
    // them), copying what it sends into sent (the app's last state sent).
    void WriteState(Writer &w, const int *raw, int *sent);

    // True once each time the hello goes out (to a new page, or on
    // request): the app should start its buffer overview from scratch.
    bool TakeRestart();

  private:
    const Param      *params_     = nullptr;
    int               num_params_ = 0;
    volatile uint32_t last_heard_ = 0;
    volatile bool     heard_      = false;
    volatile bool     requested_  = false;
    int               next_param_ = -1; // -1: hello next
    int               next_value_ = 0;  // the state's next run, until num_params_
    bool              restart_    = false;
    uint32_t          last_state_ = 0;
    uint32_t          last_frame_ = 0;
};
} // namespace weblink
