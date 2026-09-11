#pragma once
#include "PrototypeRenderer.h"
#include <cstdint>
#include <map>
#include <utility>

// Never convert absolute chunk coordinates into floating-point world positions.
struct Location {
    int64_t cx=0, cy=0;
    double x=90, y=90;
    void Normalize();
};
struct CityChunk { uint32_t seed; float heights[4]; };
class PrototypeWorld {
public:
    PrototypeWorld();
    void Update(float dt);
    void Key(unsigned char key, bool down);
    void Draw(PrototypeRenderer& r);
    void DrawUI(PrototypeRenderer& r);
    static bool SelfTest();
private:
    using Coord=std::pair<int64_t,int64_t>;
    static CityChunk Generate(Coord coord);
    void Stream();
    bool Blocked(const Location& p) const;
    void Move(float dx,float dy);
    Point Project(float x,float y,float z=0) const;
    void Floor(PrototypeRenderer& r,float x,float y,const CityChunk& chunk);
    void Building(PrototypeRenderer& r,float x,float y,float w,float d,float height,uint32_t seed);
    void Player(PrototypeRenderer& r);
    void Rift(PrototypeRenderer& r,float x,float y);
    void Hud(PrototypeRenderer& r);
    std::map<Coord,CityChunk> chunks_;
    Location player_;
    bool keys_[256]={};
    bool weapon_=false, distortion_=true, memory_=false, debug_=false;
    float time_=0,attack_=0,transform_=0,walk_=0,zoom_=1;
    Point center_{640,400},cameraLag_{0,0},facing_{1,0};
};
