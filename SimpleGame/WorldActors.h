#pragma once
#include "Actor.h"
#include "ActorRenderContext.h"
#include "WorldTypes.h"

class ChunkActor : public Actor
{
  public:
    explicit ChunkActor(const CityChunk& chunk) : Actor("Chunk"), data(chunk)
    {
        tickEnabled = false;
    }

    CityChunk data;
    std::vector<ActorId> buildings;
    bool populated = false;
};

class FloorActor : public Actor
{
  public:
    explicit FloorActor(const CityChunk& chunk) : Actor("Floor"), data(chunk)
    {
        tickEnabled = false;
        layer = RenderLayer::Ground;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    CityChunk data;
};

class BuildingActor : public Actor
{
  public:
    BuildingActor(float width, float depth, float height, uint32_t seed)
        : Actor("Building"), width(width), depth(depth), height(height), seed(seed)
    {
        depthBias = width + depth;
        tickEnabled = false;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    float width, depth, height;
    uint32_t seed;
    int style = 0;
    int villageIndex = -1, homeSlot = -1;
};

class LampActor : public Actor
{
  public:
    LampActor() : Actor("Lamp")
    {
        tickEnabled = false;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};

class NatureActor : public Actor
{
  public:
    NatureActor(std::string slot, int index) : Actor("Nature"), slot(std::move(slot)), index(index)
    {
        tickEnabled = false;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    std::string slot;
    int index;
};

class RainActor : public Actor
{
  public:
    RainActor() : Actor("Rain")
    {
        tickEnabled = false;
        layer = RenderLayer::Weather;
        screenSpace = true;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};

class HudActor : public Actor
{
  public:
    HudActor() : Actor("HUD")
    {
        tickEnabled = false;
        layer = RenderLayer::UI;
        screenSpace = true;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};
