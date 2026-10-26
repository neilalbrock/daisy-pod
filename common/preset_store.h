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
// reorders parameters: whatever isn't stored keeps its value. A checksum
// catches a record cut short by a power cut, which then reads as empty.
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
};

class PresetStore
{
  public:
    static constexpr int kSlots      = 8;
    static constexpr int kGlobals    = kSlots; // the record for global settings
    static constexpr int kMaxEntries = 96;     // a record's (key, value) pairs

    static constexpr uint32_t kBase       = 0x700000; // the last 1 MB of the chip
    static constexpr uint32_t kRegionSize = 0x10000;
    static constexpr uint32_t kSector     = 0x1000;

    struct Entry
    {
        uint16_t key;
        uint16_t value;
    };

    void Init(Flash *flash, int region);

    // record: a slot 0..kSlots - 1, or kGlobals. Read() copies up to `max`
    // entries and returns how many, or 0 if the record is empty or damaged.
    int  Read(int record, Entry *out, int max) const;
    bool Write(int record, const Entry *entries, int n);
    bool Has(int record) const { return Read(record, nullptr, 0) > 0; }

    // Empties the whole region: every preset and the globals.
    bool EraseAll();

    // The key for a parameter's name.
    static uint16_t Key(const char *name);

  private:
    struct Header
    {
        uint32_t magic;
        uint16_t version;
        uint16_t count;
        uint32_t checksum;
    };

    uint32_t Offset(int record) const;

    Flash   *flash_  = nullptr;
    uint32_t region_ = 0;
};
