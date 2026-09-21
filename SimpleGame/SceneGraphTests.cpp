#include "stdafx.h"
#include "SceneGraph.h"
#include "ActorRenderContext.h"
#include <functional>
#include <iostream>

namespace
{
    struct TestRenderer : ActorRenderContext
    {
        void Draw(const PlayerActor&) override
        {
        }

        void Draw(const WeaponActor&) override
        {
        }

        void Draw(const RangeActor&) override
        {
        }

        void Draw(const EnemyActor&) override
        {
        }

        void Draw(const ProjectileActor&) override
        {
        }

        void Draw(const LootActor&) override
        {
        }

        void Draw(const EffectActor&) override
        {
        }

        void Draw(const PortalActor&) override
        {
        }

        void Draw(const TelegraphActor&) override
        {
        }

        void Draw(const FloorActor&) override
        {
        }

        void Draw(const BuildingActor&) override
        {
        }

        void Draw(const LampActor&) override
        {
        }

        void Draw(const RainActor&) override
        {
        }

        void Draw(const HudActor&) override
        {
        }
    };

    class Probe : public Actor
    {
      public:
        Probe() : Actor("Probe")
        {
        }

        int updates = 0;
        std::function<void(Probe&)> tick;
        std::vector<ActorId>* rendered = nullptr;

        void Update(float, ActorUpdateContext&) override
        {
            ++updates;
            if (tick)
            {
                tick(*this);
            }
        }

        void Render(ActorRenderContext&) const override
        {
            if (rendered)
            {
                rendered->push_back(Id());
            }
        }
    };
} // namespace

bool SceneGraph::SelfTest()
{
    bool ok = true;
    auto check = [&](bool pass, const char* name)
    {
        std::cout << (pass ? "PASS " : "FAIL ") << name << '\n';
        ok = ok && pass;
    };
    SceneGraph graph;
    ActorUpdateContext context;
    TestRenderer renderer;
    auto& root = graph.Spawn<Probe>(NoActor);
    root.local = {10, 20};
    auto& child = graph.Spawn<Probe>(root.Id());
    child.local = {3, 4};
    auto& leaf = graph.Spawn<Probe>(child.Id());
    leaf.local = {2, 1};
    check(leaf.Position().x == 15 && leaf.Position().y == 25,
          "scene graph inherits ancestor transforms");
    auto& other = graph.Spawn<Probe>(NoActor);
    other.local = {-20, 50};
    check(graph.Reparent(child.Id(), other.Id()) && leaf.Position().x == 15
              && leaf.Position().y == 25,
          "reparent preserves world position");
    check(!graph.Reparent(other.Id(), leaf.Id()) && !graph.Reparent(root.Id(), root.Id()),
          "scene graph rejects cycles");
    graph.Reparent(child.Id(), root.Id());
    root.enabled = false;
    graph.Update(.01f, context);
    check(root.updates == 0 && child.updates == 0 && leaf.updates == 0,
          "disabled ancestor suppresses subtree updates");
    root.enabled = true;
    std::vector<ActorId> drawn;
    root.rendered = child.rendered = leaf.rendered = &drawn;
    root.visible = false;
    graph.Render(renderer, RenderLayer::Ground, RenderLayer::UI);
    check(drawn.empty(), "hidden ancestor suppresses subtree rendering");
    root.visible = true;
    graph.Rebase({7, 8});
    check(leaf.Position().x == 8 && leaf.Position().y == 17,
          "origin rebasing transforms roots only");
    ActorId rootId = root.Id(), childId = child.Id(), leafId = leaf.Id();
    graph.Destroy(rootId);
    check(!graph.Get(childId) && !graph.Get(leafId),
          "destroyed ancestor immediately hides children from queries");
    graph.CollectDestroyed();
    check(graph.Size() == 1, "deferred destruction collects the entire subtree");
    auto& fresh = graph.Spawn<Probe>(NoActor);
    check(fresh.Id() > leafId && !graph.Get(rootId), "actor handles are never reused");

    SceneGraph during;
    ActorId newborn = NoActor;
    auto& spawner = during.Spawn<Probe>(NoActor);
    spawner.tick = [&](Probe& self)
    {
        if (!newborn)
        {
            newborn = self.Scene().Spawn<Probe>(NoActor).Id();
        }
    };
    during.Update(.01f, context);
    check(during.Get<Probe>(newborn)->updates == 0,
          "actors spawned during update begin on next traversal");
    during.Update(.01f, context);
    check(during.Get<Probe>(newborn)->updates == 1, "new actors update on the next frame");
    auto& doomed = during.Spawn<Probe>(NoActor);
    ActorId doomedId = doomed.Id();
    spawner.tick = [&](Probe& self)
    {
        self.Scene().Destroy(doomedId);
        self.Scene().CollectDestroyed();
    };
    during.Update(.01f, context);
    check(!during.Get(doomedId), "destruction during traversal is safe and skips pending actors");

    SceneGraph ordered;
    drawn.clear();
    auto& front = ordered.Spawn<Probe>(NoActor);
    front.local = {10, 10};
    front.rendered = &drawn;
    auto& back = ordered.Spawn<Probe>(NoActor);
    back.rendered = &drawn;
    auto& ground = ordered.Spawn<Probe>(NoActor);
    ground.layer = RenderLayer::Ground;
    ground.rendered = &drawn;
    ordered.Render(renderer, RenderLayer::Ground, RenderLayer::World);
    check(drawn == std::vector<ActorId>{ground.Id(), back.Id(), front.Id()},
          "scene traversal preserves layer and isometric depth order");
    bool invalid = false;
    try
    {
        ordered.Spawn<Probe>(999999);
    }
    catch (const std::invalid_argument&)
    {
        invalid = true;
    }
    check(invalid, "scene graph rejects missing parents");
    auto& screen = ordered.Spawn<Probe>(NoActor);
    screen.screenSpace = true;
    screen.local = {7, 8};
    ordered.Rebase({100, 100});
    check(screen.Position().x == 7 && screen.Position().y == 8,
          "screen-space roots ignore world rebasing");
    auto& attached = ordered.Spawn<Probe>(NoActor);
    attached.local = {3, 4};
    check(ordered.Reparent(attached.Id(), screen.Id(), false) && attached.Position().x == 10
              && attached.Position().y == 12,
          "reparent can retain local coordinates");
    return ok;
}
