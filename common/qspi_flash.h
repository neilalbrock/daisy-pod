// The Seed's QSPI flash, for PresetStore (common/preset_store.h)
//
// Reads come straight from the memory-mapped chip. A write erases the
// sector and programs it, which takes the chip out of memory-mapped mode
// for up to 0.4 s (libDaisy puts it back): nothing else may read it then,
// so write only from the main loop. Afterwards the data cache is told the
// sector has changed.
//
// Unlike the rest of common/, this touches the hardware, so only main.cpp
// should include it.

#pragma once

#include "daisy_seed.h"
#include "preset_store.h"
#include "sys/dma.h"

class QspiFlash : public Flash
{
  public:
    explicit QspiFlash(daisy::QSPIHandle &qspi) : qspi_(qspi) {}

    const uint8_t *Read(uint32_t offset) override
    {
        return static_cast<const uint8_t *>(qspi_.GetData(offset));
    }

    bool Write(uint32_t offset, const uint8_t *data, size_t size) override
    {
        if(qspi_.EraseSector(offset) != daisy::QSPIHandle::Result::OK)
            return false;
        bool ok = qspi_.Write(offset, (uint32_t)size, const_cast<uint8_t *>(data))
                  == daisy::QSPIHandle::Result::OK;
        dsy_dma_invalidate_cache_for_buffer(static_cast<uint8_t *>(qspi_.GetData(offset)),
                                            PresetStore::kSector);
        return ok;
    }

    bool Erase(uint32_t offset, size_t size) override
    {
        bool ok = qspi_.Erase(offset, offset + (uint32_t)size) == daisy::QSPIHandle::Result::OK;
        dsy_dma_invalidate_cache_for_buffer(static_cast<uint8_t *>(qspi_.GetData(offset)), size);
        return ok;
    }

  private:
    daisy::QSPIHandle &qspi_;
};
