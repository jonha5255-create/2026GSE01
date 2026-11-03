#pragma once
#include "Actor.h"
#include <map>
#include <memory>
#include <vector>
#include <stdexcept>
#include <utility>
#include <unordered_map>
#include <typeindex>

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
        result.parentNode_ = parent ? Get(parent) : nullptr;
        result.scene_ = this;
        actors_.emplace(result.Id(), std::move(value));
        index_.emplace(result.Id(), &result);
        queryCache_.clear();
        return result;
    }

    Actor* Get(ActorId id) const;

    template <class T> T* Get(ActorId id) const
    {
        return dynamic_cast<T*>(Get(id));
    }

    // Monitoring can include disabled actors; pending destruction is always excluded.
    template <class T> std::vector<T*> Query(bool includeInactive = false) const
    {
        std::vector<T*> result;
        auto key = std::type_index(typeid(T));
        auto found = queryCache_.find(key);
        if (found == queryCache_.end())
        {
            std::vector<Actor*> matches;
            for (const auto& entry : actors_)
            {
                if (dynamic_cast<T*>(entry.second.get()))
                {
                    matches.push_back(entry.second.get());
                }
            }
            found = queryCache_.emplace(key, std::move(matches)).first;
        }
        result.reserve(found->second.size());
        for (auto* actor : found->second)
        {
            if (includeInactive ? !Pending(actor->Id()) : Active(actor->Id()))
            {
                result.push_back(static_cast<T*>(actor));
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
    std::unordered_map<ActorId, Actor*> index_;
    mutable std::unordered_map<std::type_index, std::vector<Actor*>> queryCache_;
    ActorId nextId_ = 1;
    unsigned traversalDepth_ = 0;
};
