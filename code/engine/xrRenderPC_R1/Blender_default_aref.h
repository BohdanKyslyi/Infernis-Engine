#ifndef BLENDER_DEFAULT_AREF_H
#define BLENDER_DEFAULT_AREF_H
#pragma once

class CBlender_default_aref final : public IBlender {
public:
    xrP_Integer oAREF;
    xrP_BOOL oBlend;

public:
    CBlender_default_aref();
    ~CBlender_default_aref() override = default;

    [[nodiscard]] LPCSTR getComment() override { return "LEVEL: lmap*base.aref"; }
    [[nodiscard]] BOOL canBeDetailed() override { return TRUE; }
    [[nodiscard]] BOOL canBeLMAPped() override { return TRUE; }

    void Save(IWriter& fs) override;
    void Load(IReader& fs, u16 version) override;

    void Compile(CBlender_Compile& C) override;
};

#endif // BLENDER_DEFAULT_AREF_H