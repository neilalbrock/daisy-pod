// Records of parameter values in flash: each app's presets and settings
//
// Every app has its own region of the Seed's 8 MB QSPI flash, far above
// the bootloader's first 256 KB and the app image it keeps at 0x40000
// (480 KB at most). So flashing an app never touches a region, and
// switching between apps keeps each one's presets. A region is 16 sectors
// of 4 KB, the smallest the flash erases: sector 0 holds the app's global
// settings, sectors 1-8 its presets.
//
// A record is a header and (key, value) pairs, a key being a hash of the
// parameter's name, so a record still loads after the app gains, loses or
// reorders parameters: whatever isn't stored keeps its value. Two names
// must not hash alike: Key() is constexpr so that each app checks its list
// when it builds (UniqueKeys in presets.h). A checksum catches a record cut
// short by a power cut, which then reads as empty.
//
// libDaisy-free: the flash comes through the Flash interface
// (common/qspi_flash.h on the Seed). Read and write it from one place only
// (the main loop): erasing makes the flash unreadable for a while.

#pragma once

#include <cstddef>
#include <cstdint>

class Flash
{
  public:
    // Memory-mapped contents at an offset into the chip.
    virtual const uint8_t *Read(uint32_t offset) = 0;
    // Erases the 4 KB sector at `offset`, then writes `size` bytes there.
    virtual bool Write(uint32_t offset, const uint8_t *data, size_t size) = 0;
    // Erases `size` bytes from `offset` (whole 64 KB blocks where it can).
    virtual bool Erase(uint32_t offset, size_t size) = 0;
};

// Each app's region, fixed for good once it has presets: a new app takes
// the next number.
enum PresetRegion
{
    kRegionGrainfall = 0,
    kRegionTapeworn  = 1,
    kRegionDriftnet  = 2,
    kRegionSlowdrift = 3,
    kRegionWavebyte  = 4,
};

class PresetStore
{
  public:
    static constexpr int kSlots   = 8;
    static constexpr int kGlobals = kSlots; // the record for global settings

    static constexpr uint32_t kBase       = 0x700000; // the last 1 MB of the chip
    static constexpr uint32_t kRegionSize = 0x10000;
    static constexpr uint32_t kSector     = 0x1000;

    struct Entry
    {
        uint16_t key;
        uint16_t value;
    };

    struct Header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t count;
        uint32_t checksum;
    };

    // A record's (key, value) pairs: as many as fill a sector.
    static constexpr int kMaxEntries = (int)((kSector - sizeof(Header)) / sizeof(Entry));

    // Bytes for a record of n entries, as Write() takes it.
    static constexpr size_t RecordSize(int n) { return sizeof(Header) + n * sizeof(Entry); }
    // Where the entries go in such a buffer (4-byte aligned).
    static Entry *Entries(uint8_t *record) { return reinterpret_cast<Entry *>(record + sizeof(Header)); }

    void Init(Flash *flash, int region);

    // record: a slot 0..kSlots - 1, or kGlobals. Read() returns its entries
    // where they lie in the memory-mapped flash, with *n set, or nullptr
    // (and 0) if the record is empty or damaged. They stay valid until the
    // next Write() or EraseAll().
    const Entry *Read(int record, int *n) const;
    bool         Has(int record) const;

    // `record` holds RecordSize(n) bytes, with the n entries filled in at
    // Entries(record): Write() adds the header and writes the lot.
    bool Write(int record, uint8_t *buf, int n);

    // Empties the whole region: every preset and the globals.
    bool EraseAll();

    // The key for a parameter's name: FNV-1a, folded to 16 bits.
    static constexpr uint16_t Key(const char *name)
    {
        uint32_t h = 2166136261u;
        while(*name)
            h = (h ^ (uint8_t)*name++) * 16777619u;
        return (uint16_t)(h ^ (h >> 16));
    }

  private:
    uint32_t Offset(int record) const;

    Flash   *flash_  = nullptr;
    uint32_t region_ = 0;
};
