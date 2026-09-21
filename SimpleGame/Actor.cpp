#include "stdafx.h"
#include "Actor.h"
#include "SceneGraph.h"

SceneGraph& Actor::Scene() const
{
    if (!scene_)
    {
        throw std::logic_error("Actor is not attached to a scene");
    }
    return *scene_;
}

ActorPosition Actor::Position() const
{
    return Scene().WorldPosition(id_);
}

void Actor::SetPosition(ActorPosition world)
{
    ActorPosition parent = Scene().WorldPosition(parent_);
    local = {world.x - parent.x, world.y - parent.y};
}

void Actor::Destroy()
{
    Scene().Destroy(id_);
}
