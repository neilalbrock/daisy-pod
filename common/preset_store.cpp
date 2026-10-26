#include "preset_store.h"

#include <cstring>

namespace
{
constexpr uint32_t kMagic   = 0x54535044; // "DPST"
constexpr uint16_t kVersion = 1;

// FNV-1a, 32 bits.
uint32_t Fnv(const uint8_t *data, size_t size, uint32_t hash = 2166136261u)
{
    for(size_t i = 0; i < size; i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
} // namespace

void PresetStore::Init(Flash *flash, int region)
{
    flash_  = flash;
    region_ = (uint32_t)region;
}

uint32_t PresetStore::Offset(int record) const
{
    uint32_t sector = record == kGlobals ? 0 : (uint32_t)record + 1;
    return kBase + region_ * kRegionSize + sector * kSector;
}

int PresetStore::Read(int record, Entry *out, int max) const
{
    if(flash_ == nullptr || record < 0 || record > kGlobals)
        return 0;
    const uint8_t *data = flash_->Read(Offset(record));
    Header         h;
    memcpy(&h, data, sizeof(h));
    if(h.magic != kMagic || h.version != kVersion || h.count == 0 || h.count > kMaxEntries)
        return 0;
    const uint8_t *entries = data + sizeof(Header);
    if(Fnv(entries, h.count * sizeof(Entry)) != h.checksum)
        return 0;
    int n = h.count < max ? h.count : max;
    if(out != nullptr && n > 0)
        memcpy(out, entries, n * sizeof(Entry));
    return out != nullptr ? n : h.count;
}

bool PresetStore::Write(int record, const Entry *entries, int n)
{
    if(flash_ == nullptr || record < 0 || record > kGlobals || n <= 0 || n > kMaxEntries)
        return false;
    uint8_t buf[sizeof(Header) + kMaxEntries * sizeof(Entry)];
    Header  h;
    h.magic    = kMagic;
    h.version  = kVersion;
    h.count    = (uint16_t)n;
    h.checksum = Fnv((const uint8_t *)entries, n * sizeof(Entry));
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), entries, n * sizeof(Entry));
    return flash_->Write(Offset(record), buf, sizeof(h) + n * sizeof(Entry));
}

bool PresetStore::EraseAll()
{
    return flash_ != nullptr && flash_->Erase(kBase + region_ * kRegionSize, kRegionSize);
}

uint16_t PresetStore::Key(const char *name)
{
    uint32_t h = Fnv((const uint8_t *)name, strlen(name));
    return (uint16_t)(h ^ (h >> 16));
}
