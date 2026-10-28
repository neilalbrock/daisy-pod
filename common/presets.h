// Presets and global settings for an app
//
// Built on the app's web parameter list (web_link.h): each Param's `store`
// says whether it belongs to a preset (the sound), the app's global
// settings (mono in, trails...) or neither (on/off, momentary switches),
// and the app's own get and set functions read and write the values, as for
// the web page. So listing a parameter is all it takes to save it.
//
// Eight slots. Selecting one loads the preset in it; an empty slot is only
// selected, keeping the sound, ready to save into. Selecting the slot
// already selected loads it again, dropping any changes. Saving writes the
// current settings to the selected slot. The global settings and the slot
// selected are saved a few seconds after they last change, and at power-up
// both come back: the globals, then the preset.
//
// A factory reset empties the app's region of the flash and puts every
// stored setting back to the app's defaults, as they were before Init
// applied anything saved. Init does it at power-up when asked (both buttons
// held: see factory_reset.h), and Reset() while running.
//
// Its arrays are sized by the app: declare a PresetsFor<kNumParams>, and
// check the list's names with UniqueKeys where it's defined. They take 18
// bytes a parameter, in DTCM with the app's other globals; an app short of
// DTCM can keep them in SDRAM instead with a PresetsIn (below).
//
// Threads: the audio callback asks (Select, Step, Save) and applies what
// the main loop has read (Apply), so it stays the only writer of the app's
// state. The main loop does all the flash work (Poll): an erase blocks for
// up to 0.4 s and leaves the flash unreadable meanwhile.
//
// libDaisy-free.

#pragma once

#include <cstdint>

#include "preset_store.h"
#include "web_link.h"

// The key the slot last selected is saved under, with the globals.
constexpr const char *kPresetSlotName = "@slot";

// True if no two of the parameters' names (nor one and the slot's) have
// the same key, so each value comes back where it was saved. For a
// static_assert beside the app's parameter list.
constexpr bool UniqueKeys(const weblink::Param *params, int n)
{
    for(int i = 0; i < n; i++)
    {
        if(PresetStore::Key(params[i].name) == PresetStore::Key(kPresetSlotName))
            return false;
        for(int j = i + 1; j < n; j++)
            if(PresetStore::Key(params[i].name) == PresetStore::Key(params[j].name))
                return false;
    }
    return true;
}

class Presets
{
  public:
    static constexpr int kSlots = PresetStore::kSlots;
    // Stored parameters, plus the slot saved with the globals.
    static constexpr int kMaxParams = PresetStore::kMaxEntries - 1;
    static constexpr int kArrays    = 6; // of values, each num_params long: see below

    using GetFn = int (*)(int index);
    using SetFn = void (*)(int index, int raw);
    using MapFn = int (*)(int index); // a stored value's parameter, or -1 to leave it

    // Before audio starts: reads the store, then applies the global
    // settings and the preset last selected through `set`. With
    // factory_reset, empties the store instead, leaving the defaults.
    void Init(Flash *flash, int region, const weblink::Param *params, int num_params,
              GetFn get, SetFn set, bool factory_reset = false);

    // From the audio callback.
    void Select(int slot); // 0..kSlots - 1
    void Step(int inc);    // from the slot selected, stopping at the ends
    void Save();           // to the slot selected (the first, if none yet)
    void Save(int slot);   // to this slot, selecting it
    void Reset();          // factory reset
    void Apply();          // once per block: applies a preset the main loop has read

    // Part of a stored preset into the current sound, as wavebyte's Load
    // Layer takes one layer of one: each value the slot holds goes to the
    // parameter map(index) gives, or nowhere for -1. The slot stays as
    // selected, and the sound shows as modified. An empty slot does
    // nothing. map is called from Apply, in the callback.
    void LoadPart(int slot, MapFn map);

    // A command from the web page: true if it was a preset's (select, save
    // or reset), which this has taken; otherwise the app's to handle.
    bool Handle(const weblink::Command &c);

    // From the main loop, every pass.
    void Poll(uint32_t now_ms);

    // For the LEDs and the web page.
    int      Slot() const { return slot_; } // -1 until one is selected
    bool     Used(int slot) const { return slot >= 0 && (used_ & (1u << slot)) != 0; }
    bool     Modified() const { return modified_; }
    uint32_t SavedAt() const { return saved_at_; } // ms, 0 if not since power-up
    void     WriteSection(weblink::Writer &w) const; // the frame's kPresets section

    // LED 2 on the preset page: the slot's colour (a hue each), dim if it's
    // empty, blinking off briefly while modified, flickering white just
    // after a save; dim white before any slot is selected.
    void LedColour(uint32_t now_ms, float *r, float *g, float *b) const;

  protected:
    // PresetsFor's arrays: keys, kArrays x capacity values, and a record's
    // buffer for capacity + 1 entries.
    Presets(int capacity, uint16_t *keys, int16_t *values, uint8_t *record)
    : capacity_(capacity),
      keys_(keys),
      pending_(values),
      defaults_(values + capacity),
      loaded_(values + 2 * capacity),
      seen_(values + 3 * capacity),
      saved_(values + 4 * capacity),
      scratch_(values + 5 * capacity),
      record_(record)
    {
    }

  private:
    static constexpr uint32_t kGlobalsDelayMs = 3000; // after the last change

    bool Load(int slot, int16_t *values); // -1 for what the slot doesn't hold
    void Snapshot(weblink::Store store, int16_t *values) const;
    bool Differs(weblink::Store store, const int16_t *a, const int16_t *b) const;
    void SaveGlobals();

    PresetStore           store_;
    const weblink::Param *params_     = nullptr;
    int                   num_params_ = 0;
    GetFn                 get_        = nullptr;
    SetFn                 set_        = nullptr;
    int                   capacity_;
    uint16_t             *keys_;

    // Callback to main loop: requests, -1 for none.
    volatile int  select_req_ = -1, save_req_ = -1, part_req_ = -1;
    MapFn         part_map_ = nullptr; // for part_req_, and then for the values pending
    volatile bool reset_req_  = false;
    int          target_     = -1; // the callback's idea of the slot, ahead of slot_

    // Main loop to callback: a preset read and waiting to be applied, then
    // back once it has been.
    int16_t      *pending_;
    volatile bool pending_ready_ = false, applied_ = false;
    volatile bool pending_part_  = false; // they're part of a preset: map them, and keep loaded_

    // The main loop's.
    volatile int      slot_     = -1;
    volatile uint32_t used_     = 0; // a bit per slot holding a preset
    volatile bool     modified_ = false;
    volatile uint32_t saved_at_ = 0;
    int16_t          *defaults_; // every stored setting, before Init
    int16_t          *loaded_;   // the preset as selected or saved
    int16_t          *seen_;     // the globals, as last seen
    int16_t          *saved_;    // ... and as last saved
    int16_t          *scratch_;  // for a snapshot
    uint8_t          *record_;   // a record being written
    int               seen_slot_ = -1, saved_slot_ = -1;
    uint32_t          changed_at_ = 0;
};

// Presets for an app with num_params parameters.
template <int num_params>
class PresetsFor : public Presets
{
    static_assert(num_params <= kMaxParams, "too many parameters for a preset");

  public:
    PresetsFor() : Presets(num_params, keys_, values_, record_) {}

  private:
    uint16_t keys_[num_params]               = {};
    int16_t  values_[kArrays * num_params]   = {};
    alignas(4) uint8_t record_[PresetStore::RecordSize(num_params + 1)] = {};
};

// The same arrays, for an app to place itself, such as in SDRAM:
//
//   PresetsMemory<kNumParams> DSY_SDRAM_BSS preset_memory;
//   PresetsIn<kNumParams>                   presets(&preset_memory);
//
// SDRAM only runs once hw.Init has set it up, so nothing may write it from
// a static constructor: PresetsIn's only keeps the pointers, and Init
// clears the arrays.
template <int num_params>
struct PresetsMemory
{
    uint16_t keys[num_params];
    int16_t  values[Presets::kArrays * num_params];
    alignas(4) uint8_t record[PresetStore::RecordSize(num_params + 1)];
};

template <int num_params>
class PresetsIn : public Presets
{
    static_assert(num_params <= kMaxParams, "too many parameters for a preset");

  public:
    explicit PresetsIn(PresetsMemory<num_params> *m) : Presets(num_params, m->keys, m->values, m->record) {}
};
