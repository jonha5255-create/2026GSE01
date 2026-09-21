#pragma once
#include <cstdint>
#include <string>
#include <utility>

using ActorId = uint64_t;
constexpr ActorId NoActor = 0;

struct ActorPosition
{
    float x = 0, y = 0;
};

class SceneGraph;
class ActorRenderContext;

struct ActorUpdateContext
{
    virtual ~ActorUpdateContext() = default;
};

enum class RenderLayer
{
    Ground,
    Telegraph,
    World,
    Projectile,
    Effect,
    Weather,
    UI
};

// Noncopyable scene-owned node. Local translation is relative to its parent.
class Actor
{
  public:
    explicit Actor(std::string name = "Actor") : name_(std::move(name))
    {
    }

    virtual ~Actor() = default;
    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    ActorId Id() const
    {
        return id_;
    }

    ActorId Parent() const
    {
        return parent_;
    }

    const std::string& Name() const
    {
        return name_;
    }

    ActorPosition Position() const;
    void SetPosition(ActorPosition world);
    ActorPosition local;
    bool enabled = true, visible = true, screenSpace = false;
    RenderLayer layer = RenderLayer::World;
    float depthBias = 0;

    virtual int UpdateOrder() const
    {
        return 0;
    }

    virtual void Update(float, ActorUpdateContext&)
    {
    }

    virtual void Render(ActorRenderContext&) const
    {
    }

    SceneGraph& Scene() const;
    void Destroy();

  private:
    friend class SceneGraph;
    ActorId id_ = NoActor, parent_ = NoActor;
    SceneGraph* scene_ = nullptr;
    std::string name_;
    bool destroyed_ = false;
};
