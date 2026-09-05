#pragma once

#include "../xrServerEntities/inventory_space.h"

namespace NoirInventorySlots {

bool Enabled();
bool KnifeEnabled();
bool BinocularEnabled();
bool TorchEnabled();
bool ExtraPistolEnabled();
bool BackpackEnabled();

bool IsSlotEnabled(u16 slot_id);

} // namespace NoirInventorySlots
