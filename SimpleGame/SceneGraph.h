#pragma once
#include "Actor.h"
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>
#include <utility>

class SceneGraph
{
  public:
    SceneGraph() = default;
    SceneGraph(const SceneGraph&) = delete;
    SceneGraph& operator=(const SceneGraph&) = delete;

    template <class T, class... Args> T& Spawn(ActorId parent, Args&&... args)
    {
        if (parent != NoActor && !Get(parent))
        {
            throw std::invalid_argument("Invalid actor parent");
        }
        auto value = std::make_unique<T>(std::forward<Args>(args)...);
        T& result = *value;
        result.id_ = nextId_++;
        result.parent_ = parent;
        result.scene_ = this;
        actors_.emplace(result.Id(), std::move(value));
        return result;
    }

    Actor* Get(ActorId id) const;

    template <class T> T* Get(ActorId id) const
    {
        return dynamic_cast<T*>(Get(id));
    }

    template <class T> std::vector<T*> Query() const
    {
        std::vector<T*> result;
        for (const auto& entry : actors_)
        {
            if (Active(entry.first))
            {
                if (auto actor = dynamic_cast<T*>(entry.second.get()))
                {
                    result.push_back(actor);
                }
            }
        }
        return result;
    }

    bool Active(ActorId id) const;
    bool Visible(ActorId id) const;
    ActorPosition WorldPosition(ActorId id) const;
    bool Reparent(ActorId id, ActorId parent, bool preserveWorld = true);
    void Destroy(ActorId id);
    void CollectDestroyed();
    void Update(float dt, ActorUpdateContext& context);
    void Render(ActorRenderContext& context, RenderLayer first, RenderLayer last);
    void Rebase(ActorPosition delta);

    size_t Size() const
    {
        return actors_.size();
    }

    static bool SelfTest();

  private:
    bool Pending(ActorId id) const;
    std::map<ActorId, std::unique_ptr<Actor>> actors_;
    ActorId nextId_ = 1;
    unsigned traversalDepth_ = 0;
};
