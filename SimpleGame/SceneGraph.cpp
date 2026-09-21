#include "stdafx.h"
#include "SceneGraph.h"
#include <algorithm>

bool SceneGraph::Pending(ActorId id) const
{
    while (id)
    {
        auto found = actors_.find(id);
        if (found == actors_.end() || found->second->destroyed_)
        {
            return true;
        }
        id = found->second->parent_;
    }
    return false;
}

Actor* SceneGraph::Get(ActorId id) const
{
    auto found = actors_.find(id);
    return found == actors_.end() || Pending(id) ? nullptr : found->second.get();
}

bool SceneGraph::Active(ActorId id) const
{
    if (!Get(id))
    {
        return false;
    }
    while (id)
    {
        const auto& actor = *actors_.at(id);
        if (!actor.enabled)
        {
            return false;
        }
        id = actor.parent_;
    }
    return true;
}

bool SceneGraph::Visible(ActorId id) const
{
    if (!Active(id))
    {
        return false;
    }
    while (id)
    {
        const auto& actor = *actors_.at(id);
        if (!actor.visible)
        {
            return false;
        }
        id = actor.parent_;
    }
    return true;
}

ActorPosition SceneGraph::WorldPosition(ActorId id) const
{
    ActorPosition result;
    while (id)
    {
        auto found = actors_.find(id);
        if (found == actors_.end())
        {
            break;
        }
        result.x += found->second->local.x;
        result.y += found->second->local.y;
        id = found->second->parent_;
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
    for (ActorId id : dead)
    {
        actors_.erase(id);
    }
}

void SceneGraph::Update(float dt, ActorUpdateContext& context)
{
    std::vector<ActorId> snapshot;
    for (const auto& entry : actors_)
    {
        if (Active(entry.first))
        {
            snapshot.push_back(entry.first);
        }
    }
    std::stable_sort(snapshot.begin(),
                     snapshot.end(),
                     [&](ActorId a, ActorId b)
                     {
                         return actors_.at(a)->UpdateOrder() < actors_.at(b)->UpdateOrder();
                     });
    ++traversalDepth_;
    try
    {
        for (ActorId id : snapshot)
        {
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
    std::vector<ActorId> snapshot;
    for (const auto& entry : actors_)
    {
        const auto& actor = *entry.second;
        if (Visible(entry.first) && actor.layer >= first && actor.layer <= last)
        {
            snapshot.push_back(entry.first);
        }
    }
    std::stable_sort(snapshot.begin(),
                     snapshot.end(),
                     [&](ActorId a, ActorId b)
                     {
                         const Actor &left = *actors_.at(a), &right = *actors_.at(b);
                         if (left.layer != right.layer)
                         {
                             return left.layer < right.layer;
                         }
                         auto p = left.Position(), q = right.Position();
                         return p.x + p.y + left.depthBias < q.x + q.y + right.depthBias;
                     });
    ++traversalDepth_;
    try
    {
        for (ActorId id : snapshot)
        {
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
