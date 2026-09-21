#pragma once
#include "Actor.h"
#include "ActorRenderContext.h"
#include "WorldTypes.h"

class ChunkActor : public Actor
{
  public:
    explicit ChunkActor(const CityChunk& chunk) : Actor("Chunk"), data(chunk)
    {
    }

    CityChunk data;
    ActorId buildings[4] = {};
};

class FloorActor : public Actor
{
  public:
    explicit FloorActor(const CityChunk& chunk) : Actor("Floor"), data(chunk)
    {
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
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }

    float width, depth, height;
    uint32_t seed;
};

class LampActor : public Actor
{
  public:
    LampActor() : Actor("Lamp")
    {
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};

class RainActor : public Actor
{
  public:
    RainActor() : Actor("Rain")
    {
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
        layer = RenderLayer::UI;
        screenSpace = true;
    }

    void Render(ActorRenderContext& r) const override
    {
        r.Draw(*this);
    }
};
