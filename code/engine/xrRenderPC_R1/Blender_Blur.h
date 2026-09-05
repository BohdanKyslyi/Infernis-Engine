#ifndef BLENDER_BLUR_H
#define BLENDER_BLUR_H
#pragma once

class CBlender_Blur final : public IBlender {
public:
    CBlender_Blur();
    ~CBlender_Blur() override = default;

    [[nodiscard]] LPCSTR getComment() override { return "INTERNAL: blur"; }
    [[nodiscard]] BOOL canBeLMAPped() override { return FALSE; }

    void Save(IWriter& fs) override;
    void Load(IReader& fs, u16 version) override;

    void Compile(CBlender_Compile& C) override;
};

#endif // BLENDER_BLUR_H