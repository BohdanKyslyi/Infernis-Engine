![photo_2024-04-02_12-25-43](https://github.com/user-attachments/assets/5c28b9e8-2009-47b5-a31a-2f761a1b607b)

# Infernis Engine
 This project is the first **Ukrainian** modified **X-Ray 1.6 Engine** for general use and play. It aims to improve game features for modmakers and players who will play mods with it. Based on the original **X-Ray Engine 1.6**, all rights belong to the company GSC Game World.

### Main updates:
— x64 Architecture

— Added the ability to use a stationary machine gun, which was previously featured in the “Shadow of Chornobyl” mods. Previously, it was impossible to fire from it, but now everything works; modders simply need to integrate it via SDK or Perl, and it will function seamlessly in their new mods;
![photo_2024-04-02_12-25-43](https://github.com/user-attachments/assets/94fe879b-965a-466a-9566-bed44cf92bde)

— Implemented a full-fledged slot for knives;

— A full-fledged slot for binoculars has been implemented;

— A new slot for backpacks has been added (+ a new “backpack wear” parameter for armor, which can be adjusted so that the player cannot wear a backpack over an exoskeleton, “SEVA,” or your chosen jumpsuit).
![photo_2024-04-02_12-25-42](https://github.com/user-attachments/assets/a280056a-ffea-41c8-b22a-9e8dac8b49c2)

— Console commands and a Debug menu have been added

— The currency has been changed from rubles to hryvnias;

— An issue where the back side of the weapon HUD model was clipping with the camera has been fixed. This will allow for more “accurate” weapon positioning/aiming in weapon packs based on the Infernis Engine
![photo_2026-01-09_15-34-21](https://github.com/user-attachments/assets/64aa2df8-e76f-442d-9570-2755359c4d29)

— A “raindrop” effect has been added to the screen during rain
![photo_2026-01-09_15-47-38 (2)](https://github.com/user-attachments/assets/7766e46b-060c-4eda-b19c-c9255538aed4)

— Added a “swaying” effect for trees and bushes

— Restored rank, reputation (for NPCs and the player), and NPC attitude toward the player indicators to the game interface
![photo_2026-01-09_15-47-39](https://github.com/user-attachments/assets/c2dd151e-0d84-4640-95f5-6933f0febf8d)

— Changed the cursor
![photo_2026-01-09_15-47-39 (2)](https://github.com/user-attachments/assets/d57eee48-22fe-43e0-bd13-9154de4ee213)

— Added consumable items use animations
![photo_2026-01-09_15-47-39 (2)](https://github.com/user-attachments/assets/50939109-dfa3-436b-b8eb-255a74e091fc)

— Added a metallic/roughness PBR material pipeline for the DX11 renderer. See
[PBR material authoring](PBR_MATERIALS.md) for texture packing and setup.

### How do I build the updated engine?
1) [Install **CMake**](https://cmake.org/download/) version **3.5** or higher
2) **Clone the repository**
3) Open the **“Build”** folder in the terminal
4) Enter the following command:
`cmake .. -G "Visual Studio 17 2022" -A x64`
For **Visual Studio 2022**, or
`cmake .. -G "Visual Studio 18 2026" -A x64`
For **Visual Studio 2026**, or similar code for compiling on other versions of Visual Studio
5) Open the **“infernis-engine.sln”** file in the **“Build”** folder
6) Set the **“RelWithDebInfo”** compilation option and build the project

### Join the Infernis' Modding Community!
**Site**: https://infernis-modding.com/

**Infernis Engine page**: https://infernis-modding.com/mods/infernis_engine

**YouTube**: https://www.youtube.com/@Lord_Infernis

**Telegram**:
- https://t.me/Infernis_Team
- https://t.me/Stalker_Modding_UA
- https://t.me/Lord_Infernis

**Discord**: https://discord.gg/xxTYFSzFz3
