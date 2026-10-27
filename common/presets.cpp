#include "presets.h"

#include <atomic>

#include "colour.h"

using weblink::Store;

namespace
{
constexpr uint16_t kSlotKey = PresetStore::Key(kPresetSlotName);
} // namespace

void Presets::Init(Flash *flash, int region, const weblink::Param *params, int num_params,
                   GetFn get, SetFn set, bool factory_reset)
{
    store_.Init(flash, region);
    params_     = params;
    num_params_ = num_params < capacity_ ? num_params : capacity_;
    get_        = get;
    set_        = set;
    for(int i = 0; i < num_params_; i++)
    {
        keys_[i]     = PresetStore::Key(params_[i].name);
        defaults_[i] = params_[i].store != Store::kNone ? (int16_t)get_(i) : -1;
    }
    if(factory_reset)
        store_.EraseAll();

    used_ = 0;
    for(int s = 0; s < kSlots; s++)
        if(store_.Has(s))
            used_ = used_ | (1u << s);

    // The globals, and the slot last selected.
    int                       n;
    const PresetStore::Entry *entries = store_.Read(PresetStore::kGlobals, &n);
    int                       slot    = -1;
    for(int e = 0; e < n; e++)
    {
        if(entries[e].key == kSlotKey)
            slot = entries[e].value < kSlots ? entries[e].value : -1;
        for(int i = 0; i < num_params_; i++)
            if(params_[i].store == Store::kGlobal && keys_[i] == entries[e].key)
                set_(i, entries[e].value);
    }

    // Then the preset.
    if(slot >= 0 && Load(slot, scratch_))
        for(int i = 0; i < num_params_; i++)
            if(scratch_[i] >= 0)
                set_(i, scratch_[i]);
    slot_   = slot;
    target_ = slot;

    Snapshot(Store::kPreset, loaded_);
    Snapshot(Store::kGlobal, seen_);
    Snapshot(Store::kGlobal, saved_);
    seen_slot_ = saved_slot_ = slot;
}

void Presets::Select(int slot)
{
    if(slot < 0 || slot >= kSlots)
        return;
    target_     = slot;
    select_req_ = slot;
}

void Presets::Step(int inc)
{
    int slot = target_ < 0 ? (inc > 0 ? 0 : kSlots - 1) : target_ + inc;
    slot     = slot < 0 ? 0 : (slot >= kSlots ? kSlots - 1 : slot);
    if(slot != target_)
        Select(slot);
}

void Presets::Save()
{
    Save(target_ < 0 ? 0 : target_);
}

void Presets::Save(int slot)
{
    if(slot < 0 || slot >= kSlots)
        return;
    target_   = slot;
    save_req_ = slot;
}

bool Presets::Handle(const weblink::Command &c)
{
    switch(c.type)
    {
        case weblink::Msg::kPresetSelect: Select(c.index); return true;
        case weblink::Msg::kPresetSave: Save(c.index); return true;
        case weblink::Msg::kPresetReset: Reset(); return true;
        default: return false;
    }
}

void Presets::Reset()
{
    target_    = -1;
    reset_req_ = true;
}

void Presets::Apply()
{
    if(!pending_ready_)
        return;
    std::atomic_signal_fence(std::memory_order_acquire);
    for(int i = 0; i < num_params_; i++)
        if(pending_[i] >= 0)
            set_(i, pending_[i]);
    pending_ready_ = false;
    applied_       = true;
}

bool Presets::Load(int slot, int16_t *values)
{
    int                       n;
    const PresetStore::Entry *entries = store_.Read(slot, &n);
    for(int i = 0; i < num_params_; i++)
    {
        values[i] = -1;
        if(params_[i].store != Store::kPreset)
            continue;
        for(int e = 0; e < n; e++)
            if(entries[e].key == keys_[i])
            {
                values[i] = (int16_t)entries[e].value;
                break;
            }
    }
    return n > 0;
}

void Presets::Snapshot(Store store, int16_t *values) const
{
    for(int i = 0; i < num_params_; i++)
        values[i] = params_[i].store == store ? (int16_t)get_(i) : 0;
}

bool Presets::Differs(Store store, const int16_t *a, const int16_t *b) const
{
    for(int i = 0; i < num_params_; i++)
        if(params_[i].store == store && a[i] != b[i])
            return true;
    return false;
}

void Presets::SaveGlobals()
{
    PresetStore::Entry *entries = PresetStore::Entries(record_);
    int                 n       = 0;
    for(int i = 0; i < num_params_; i++)
        if(params_[i].store == Store::kGlobal)
            entries[n++] = {keys_[i], (uint16_t)seen_[i]};
    if(seen_slot_ >= 0)
        entries[n++] = {kSlotKey, (uint16_t)seen_slot_};
    if(n > 0)
        store_.Write(PresetStore::kGlobals, record_, n);
    for(int i = 0; i < num_params_; i++)
        saved_[i] = seen_[i];
    saved_slot_ = seen_slot_;
}

void Presets::Poll(uint32_t now_ms)
{
    // Once a preset is applied, what it set is the reference for Modified:
    // read back, as a value may not land exactly as stored.
    if(applied_)
    {
        Snapshot(Store::kPreset, loaded_);
        applied_ = false;
    }

    // A factory reset: empty the store, and put the defaults back through
    // the callback like a preset. What's then saved matches it.
    if(reset_req_ && !pending_ready_)
    {
        reset_req_  = false;
        select_req_ = save_req_ = -1;
        store_.EraseAll();
        used_  = 0;
        slot_  = -1;
        for(int i = 0; i < num_params_; i++)
        {
            pending_[i] = defaults_[i];
            seen_[i] = saved_[i] = params_[i].store == Store::kGlobal ? defaults_[i] : 0;
        }
        seen_slot_ = saved_slot_ = -1;
        saved_at_  = now_ms == 0 ? 1 : now_ms;
        std::atomic_signal_fence(std::memory_order_release);
        pending_ready_ = true;
    }

    // Only one preset in flight: a request waits until the last is applied.
    if(!pending_ready_)
    {
        int slot = select_req_;
        if(slot >= 0)
        {
            select_req_ = -1;
            slot_       = slot;
            if(Load(slot, pending_))
            {
                std::atomic_signal_fence(std::memory_order_release);
                pending_ready_ = true;
            }
            else
                Snapshot(Store::kPreset, loaded_); // empty: the sound stays
        }

        slot = save_req_;
        if(slot >= 0 && !pending_ready_)
        {
            save_req_ = -1;
            Snapshot(Store::kPreset, loaded_);
            PresetStore::Entry *entries = PresetStore::Entries(record_);
            int                 n       = 0;
            for(int i = 0; i < num_params_; i++)
                if(params_[i].store == Store::kPreset)
                    entries[n++] = {keys_[i], (uint16_t)loaded_[i]};
            if(n > 0 && store_.Write(slot, record_, n))
                used_ = used_ | (1u << slot);
            slot_     = slot;
            saved_at_ = now_ms == 0 ? 1 : now_ms;
        }
    }

    if(!pending_ready_ && !applied_)
    {
        Snapshot(Store::kPreset, scratch_);
        modified_ = Used(slot_) && Differs(Store::kPreset, scratch_, loaded_);
    }

    // The globals and the slot: saved once they've settled.
    Snapshot(Store::kGlobal, scratch_);
    if(Differs(Store::kGlobal, scratch_, seen_) || slot_ != seen_slot_)
    {
        for(int i = 0; i < num_params_; i++)
            seen_[i] = scratch_[i];
        seen_slot_  = slot_;
        changed_at_ = now_ms;
    }
    if((Differs(Store::kGlobal, seen_, saved_) || seen_slot_ != saved_slot_)
       && now_ms - changed_at_ >= kGlobalsDelayMs)
        SaveGlobals();
}

void Presets::LedColour(uint32_t now_ms, float *r, float *g, float *b) const
{
    int slot = slot_;
    if(saved_at_ != 0 && now_ms - saved_at_ < 600)
    {
        float lit = (now_ms / 50) % 2 ? 1.0f : 0.0f;
        *r = *g = *b = lit;
        return;
    }
    if(slot < 0)
    {
        *r = *g = *b = 0.25f;
        return;
    }
    Hue((float)slot / kSlots, r, g, b);
    float level = Used(slot) ? 1.0f : 0.15f;
    if(modified_ && now_ms % 800 < 150)
        level = 0.0f;
    *r *= level;
    *g *= level;
    *b *= level;
}

void Presets::WriteSection(weblink::Writer &w) const
{
    w.U7((uint32_t)weblink::Section::kPresets);
    w.U7(slot_ < 0 ? 127 : (uint32_t)slot_);
    w.U7(modified_ ? 1 : 0);
    w.U14(used_);
}
