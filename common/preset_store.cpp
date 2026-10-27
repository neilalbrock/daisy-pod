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

const PresetStore::Entry *PresetStore::Read(int record, int *n) const
{
    *n = 0;
    if(flash_ == nullptr || record < 0 || record > kGlobals)
        return nullptr;
    const uint8_t *data = flash_->Read(Offset(record));
    Header         h;
    memcpy(&h, data, sizeof(h));
    if(h.magic != kMagic || h.version != kVersion || h.count == 0 || h.count > kMaxEntries)
        return nullptr;
    const uint8_t *entries = data + sizeof(Header);
    if(Fnv(entries, h.count * sizeof(Entry)) != h.checksum)
        return nullptr;
    *n = h.count;
    return reinterpret_cast<const Entry *>(entries);
}

bool PresetStore::Has(int record) const
{
    int n;
    return Read(record, &n) != nullptr;
}

bool PresetStore::Write(int record, uint8_t *buf, int n)
{
    if(flash_ == nullptr || record < 0 || record > kGlobals || n <= 0 || n > kMaxEntries)
        return false;
    Header h;
    h.magic    = kMagic;
    h.version  = kVersion;
    h.count    = (uint16_t)n;
    h.checksum = Fnv(buf + sizeof(Header), n * sizeof(Entry));
    memcpy(buf, &h, sizeof(h));
    return flash_->Write(Offset(record), buf, RecordSize(n));
}

bool PresetStore::EraseAll()
{
    return flash_ != nullptr && flash_->Erase(kBase + region_ * kRegionSize, kRegionSize);
}
