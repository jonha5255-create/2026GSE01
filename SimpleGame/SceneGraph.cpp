#include "stdafx.h"
#include "SceneGraph.h"
#include <algorithm>

bool SceneGraph::Pending(ActorId id) const
{
    auto found = index_.find(id);
    if (found == index_.end())
    {
        return true;
    }
    for (auto* node = found->second; node; node = node->parentNode_)
    {
        if (node->destroyed_)
        {
            return true;
        }
    }
    return false;
}

Actor* SceneGraph::Get(ActorId id) const
{
    auto found = index_.find(id);
    return found == index_.end() || Pending(id) ? nullptr : found->second;
}

bool SceneGraph::Active(ActorId id) const
{
    auto found = index_.find(id);
    if (found == index_.end())
    {
        return false;
    }
    for (auto* actor = found->second; actor; actor = actor->parentNode_)
    {
        if (actor->destroyed_ || !actor->enabled)
        {
            return false;
        }
    }
    return true;
}

bool SceneGraph::Visible(ActorId id) const
{
    auto found = index_.find(id);
    if (found == index_.end())
    {
        return false;
    }
    for (auto* actor = found->second; actor; actor = actor->parentNode_)
    {
        if (actor->destroyed_ || !actor->enabled || !actor->visible)
        {
            return false;
        }
    }
    return true;
}

ActorPosition SceneGraph::WorldPosition(ActorId id) const
{
    ActorPosition result;
    auto found = index_.find(id);
    for (auto* node = found == index_.end() ? nullptr : found->second; node;
         node = node->parentNode_)
    {
        result.x += node->local.x;
        result.y += node->local.y;
    }
    return result;
}

bool SceneGraph::Reparent(ActorId id, ActorId parent, bool preserveWorld)
{
    Actor* actor = Get(id);
    if (!actor || (parent && !Get(parent)))
    {
        return false;
    }
    for (ActorId ancestor = parent; ancestor; ancestor = actors_.at(ancestor)->parent_)
    {
        if (ancestor == id)
        {
            return false;
        }
    }
    ActorPosition position = actor->Position();
    actor->parent_ = parent;
    actor->parentNode_ = parent ? Get(parent) : nullptr;
    if (preserveWorld)
    {
        actor->SetPosition(position);
    }
    return true;
}

void SceneGraph::Destroy(ActorId id)
{
    auto found = actors_.find(id);
    if (found != actors_.end())
    {
        found->second->destroyed_ = true;
    }
}

void SceneGraph::CollectDestroyed()
{
    if (traversalDepth_)
    {
        return;
    }
    std::vector<ActorId> dead;
    for (const auto& entry : actors_)
    {
        if (Pending(entry.first))
        {
            dead.push_back(entry.first);
        }
    }
    if (!dead.empty())
    {
        queryCache_.clear();
    }
    for (ActorId id : dead)
    {
        index_.erase(id);
        actors_.erase(id);
    }
}

void SceneGraph::Update(float dt, ActorUpdateContext& context)
{
    struct Item
    {
        ActorId id;
        int order;
    };

    std::vector<Item> snapshot;
    snapshot.reserve(actors_.size());
    for (const auto& entry : actors_)
    {
        if (entry.second->tickEnabled && Active(entry.first))
        {
            snapshot.push_back({entry.first, entry.second->UpdateOrder()});
        }
    }
    std::stable_sort(snapshot.begin(),
                     snapshot.end(),
                     [](const Item& a, const Item& b)
                     {
                         return a.order < b.order;
                     });
    ++traversalDepth_;
    try
    {
        for (auto item : snapshot)
        {
            ActorId id = item.id;
            if (Active(id))
            {
                Get(id)->Update(dt, context);
            }
        }
    }
    catch (...)
    {
        --traversalDepth_;
        CollectDestroyed();
        throw;
    }
    --traversalDepth_;
    CollectDestroyed();
}

void SceneGraph::Render(ActorRenderContext& context, RenderLayer first, RenderLayer last)
{
    struct Item
    {
        ActorId id;
        RenderLayer layer;
        float depth;
    };

    std::vector<Item> snapshot;
    snapshot.reserve(actors_.size());
    for (const auto& entry : actors_)
    {
        const auto& actor = *entry.second;
        if (Visible(entry.first) && actor.layer >= first && actor.layer <= last)
        {
            auto p = actor.Position();
            snapshot.push_back({entry.first, actor.layer, p.x + p.y + actor.depthBias});
        }
    }
    std::stable_sort(snapshot.begin(),
                     snapshot.end(),
                     [](const Item& a, const Item& b)
                     {
                         if (a.layer != b.layer)
                         {
                             return a.layer < b.layer;
                         }
                         return a.depth < b.depth;
                     });
    ++traversalDepth_;
    try
    {
        for (auto item : snapshot)
        {
            ActorId id = item.id;
            if (Visible(id))
            {
                Get(id)->Render(context);
            }
        }
    }
    catch (...)
    {
        --traversalDepth_;
        CollectDestroyed();
        throw;
    }
    --traversalDepth_;
    CollectDestroyed();
}

void SceneGraph::Rebase(ActorPosition delta)
{
    for (auto& entry : actors_)
    {
        Actor& actor = *entry.second;
        if (actor.Parent() == NoActor && !actor.screenSpace)
        {
            actor.local.x -= delta.x;
            actor.local.y -= delta.y;
        }
    }
}
