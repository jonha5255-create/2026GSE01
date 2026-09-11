#include "stdafx.h"
#include "PrototypeWorld.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <vector>
#include <iostream>

namespace {
constexpr float Side=640, Pi=3.14159265f;
constexpr float RiftX=245, RiftY=45;
uint64_t Mix(uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return x^(x>>31);
}
Point Lerp(Point a,Point b,float t) { return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t}; }
float Distance(float x,float y) { return std::sqrt(x*x+y*y); }
}
void Location::Normalize() {
    // Guard at the representable edge rather than overflowing signed integers.
    while(x<0) { if(cx==INT64_MIN){x=0;break;} --cx;x+=Side; }
    while(x>=Side) { if(cx==INT64_MAX){x=Side-1;break;} ++cx;x-=Side; }
    while(y<0) { if(cy==INT64_MIN){y=0;break;} --cy;y+=Side; }
    while(y>=Side) { if(cy==INT64_MAX){y=Side-1;break;} ++cy;y-=Side; }
}
CityChunk PrototypeWorld::Generate(Coord c) {
    CityChunk chunk={};
    chunk.seed=static_cast<uint32_t>(Mix(static_cast<uint64_t>(c.first)^Mix(static_cast<uint64_t>(c.second))^2026));
    for(int i=0;i<4;++i) chunk.heights[i]=85.f+float(Mix(chunk.seed+i)%105);
    return chunk;
}
PrototypeWorld::PrototypeWorld(){ Stream(); }
void PrototypeWorld::Stream() {
    std::map<Coord,CityChunk> next;
    for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) {
        if((x<0&&player_.cx<INT64_MIN-x)||(x>0&&player_.cx>INT64_MAX-x)||
           (y<0&&player_.cy<INT64_MIN-y)||(y>0&&player_.cy>INT64_MAX-y)) continue;
        Coord c={player_.cx+x,player_.cy+y};
        auto it=chunks_.find(c);
        next.emplace(c,it!=chunks_.end()?it->second:Generate(c));
    }
    chunks_.swap(next);
}
bool PrototypeWorld::Blocked(const Location& p) const {
    // Collision uses the same footprints as rendering, with a player-radius margin.
    for(int i=0;i<4;++i) {
        float x=175.f+(i%2)*205,y=170.f+(i/2)*205;
        double dx=std::max(double(x)-p.x,std::max(0.,p.x-(x+145)));
        double dy=std::max(double(y)-p.y,std::max(0.,p.y-(y+140)));
        if(dx*dx+dy*dy<12*12) return true;
    }
    return false;
}
void PrototypeWorld::Move(float dx,float dy) {
    auto before=Coord(player_.cx,player_.cy);
    Location p=player_;p.x+=dx;p.Normalize();
    if(!Blocked(p)) { player_=p;cameraLag_.x+=dx*.85f;cameraLag_.y+=dx*.425f; }
    p=player_;p.y+=dy;p.Normalize();
    if(!Blocked(p)) { player_=p;cameraLag_.x-=dy*.85f;cameraLag_.y+=dy*.425f; }
    if(before!=Coord(player_.cx,player_.cy)) Stream();
}
void PrototypeWorld::Key(unsigned char key,bool down) {
    if(key>='A'&&key<='Z') key+=32;
    bool pressed=down&&!keys_[key];keys_[key]=down;
    if(!pressed) return;
    if(key=='q') {weapon_=!weapon_;transform_=1;}
    if(key==' '&&weapon_&&attack_<=0) attack_=.45f;
    if(key=='v') distortion_=!distortion_;
    if(key=='g') debug_=!debug_;
    if(key=='r') {player_=Location{};cameraLag_={0,0};Stream();}
    if(key=='e'&&player_.cx==0&&player_.cy==0&&Distance(float(player_.x)-RiftX,float(player_.y)-RiftY)<100)
        memory_=true;
}
void PrototypeWorld::Update(float dt) {
    dt=std::min(dt,.05f);time_+=dt;
    attack_=std::max(0.f,attack_-dt);transform_=std::max(0.f,transform_-dt*1.5f);
    float sx=float(keys_['d'])-float(keys_['a']),sy=float(keys_['s'])-float(keys_['w']);
    float length=Distance(sx,sy);
    if(length>0) {
        sx/=length;sy/=length;facing_={sx,sy};walk_+=dt*11;
        // Invert the isometric projection: WASD remains screen-relative.
        float dx=sx+sy*2,dy=-sx+sy*2;
        float norm=Distance(dx,dy);
        Move(dx/norm*180*dt,dy/norm*180*dt);
    }
    float follow=1-std::exp(-10*dt);
    cameraLag_=Lerp(cameraLag_,{0,0},follow);
}
Point PrototypeWorld::Project(float x,float y,float z) const {
    return {center_.x+((x-y)*.85f+cameraLag_.x)*zoom_,
        center_.y+((x+y)*.425f-z+cameraLag_.y)*zoom_};
}
void PrototypeWorld::Floor(PrototypeRenderer& r,float x,float y,const CityChunk& chunk) {
    auto p=[&](float a,float b){return Project(x+a,y+b);};
    r.Quad(p(0,0),p(Side,0),p(Side,Side),p(0,Side),Color(16,26,33));
    r.Quad(p(137,135),p(560,135),p(560,558),p(137,558),Color(30,42,48));
    for(int i=0;i<=8;++i) {
        float s=i*80.f;
        r.Line(p(s,0),p(s,Side),1,Color(52,66,69,.28f));
        r.Line(p(0,s),p(Side,s),1,Color(52,66,69,.28f));
    }
    r.Line(p(132,132),p(563,132),2,Color(77,99,97));
    r.Line(p(132,132),p(132,563),2,Color(77,99,97));
    r.Line(p(563,132),p(563,563),2,Color(53,67,73));
    r.Line(p(132,563),p(563,563),2,Color(53,67,73));
    for(int i=0;i<10;++i) {
        float a=i*64.f;
        r.Line(p(a,54),p(a+29,54),2,Color(154,142,95,.55f));
        r.Line(p(54,a),p(54,a+29),2,Color(154,142,95,.55f));
    }
    for(int i=0;i<7;++i) {
        float a=143.f+i*10;
        r.Quad(p(a,10),p(a+5,10),p(a+5,115),p(a,115),Color(109,128,128,.45f));
        r.Quad(p(10,a),p(115,a),p(115,a+5),p(10,a+5),Color(109,128,128,.45f));
    }
    for(int i=0;i<12;++i) {
        uint64_t n=Mix(chunk.seed+i*17);
        float a=float(n%600),b=20.f+float((n>>12)%75);
        Point q=p(a,b);
        r.Line(q,{q.x+25*zoom_,q.y-2*zoom_},2,Color(84,132,137,.22f));
        if(i<3) r.Ellipse(p(a+25,b+10),12*zoom_,4*zoom_,Color(90,28,41,.55f));
    }
    if(debug_) {
        r.Line(p(0,0),p(Side,0),2,Color(67,223,192,.8f));
        r.Line(p(0,0),p(0,Side),2,Color(67,223,192,.8f));
    }
}
void PrototypeWorld::Building(PrototypeRenderer& r,float x,float y,float w,float d,float h,uint32_t seed) {
    auto p=[&](float a,float b,float z=0){return Project(x+a,y+b,z);};
    Point a=p(0,0,h),b=p(w,0,h),c=p(w,d,h),e=p(0,d,h);
    bool haunted=(seed%5==0)&&distortion_;
    float skew=haunted?std::sin(time_*.65f+float(seed%19))*12*zoom_:0;
    a.x+=skew;b.x+=skew;c.x+=skew;e.x+=skew;
    // Fade foreground buildings when they occlude the player.
    float alpha=1;
    Point feet=Project(0,0);
    float left=std::min(a.x,std::min(b.x,std::min(c.x,e.x)));
    float right=std::max(a.x,std::max(b.x,std::max(c.x,e.x)));
    float top=std::min(a.y,std::min(b.y,std::min(c.y,e.y)));
    if(x+y>0&&feet.x+14*zoom_>left&&feet.x-14*zoom_<right&&
        feet.y>top&&feet.y-55*zoom_<p(w,d).y)
        alpha=.3f;
    auto ink=[&](int red,int green,int blue){return Color(red,green,blue,alpha);};
    r.Quad(p(0,d),p(w,d),p(w+55,d+40),p(20,d+40),Color(0,0,0,.22f));
    r.Quad(e,c,p(w,d),p(0,d),ink(30,46,53));
    r.Quad(b,c,p(w,d),p(w,0),ink(20,32,42));
    r.Quad(a,b,c,e,haunted?ink(51,47,65):ink(48,66,72));
    r.Line(a,b,2,ink(111,131,133));r.Line(a,e,2,ink(100,124,123));
    r.Line(e,c,2,ink(68,91,97));r.Line(b,c,2,ink(59,79,88));
    // Window strips run along both visible walls.
    for(float z=24;z<h-12;z+=30) for(int col=0;col<5;++col) {
        float s=12+col*27.f;
        uint64_t n=Mix(seed+col+int(z)*31);
        Ink light=n%4==0?ink(157,139,111):n%3==0?ink(64,145,151):ink(17,32,39);
        Point f1=p(s,d+.2f,z),f2=p(s+16,d+.2f,z);
        r.Quad(f1,f2,p(s+16,d+.2f,z+15),p(s,d+.2f,z+15),light);
        r.Quad(p(w+.2f,s,z),p(w+.2f,s+16,z),p(w+.2f,s+16,z+15),p(w+.2f,s,z+15),light);
    }
    // Rooftop air conditioner and antenna.
    r.Quad(p(28,30,h+1),p(72,30,h+1),p(72,66,h+1),p(28,66,h+1),ink(21,35,43));
    for(int i=0;i<5;++i) r.Line(p(33,34+i*6.f,h+2),p(67,34+i*6.f,h+2),1,ink(80,101,108));
    r.Line(p(w-25,25,h),p(w-25,25,h+38),2,ink(102,120,122));
    r.Line(p(w-25,25,h+29),p(w-5,25,h+29),1,ink(102,120,122));
    Point sign=p(w*.5f,d+1,40);
    r.Rect(sign.x-37*zoom_,sign.y-12*zoom_,74*zoom_,20*zoom_,ink(9,23,29));
    Ink neon=Emissive(haunted?ink(239,81,122):ink(104,223,204),2.5f);
    r.Line({sign.x-36*zoom_,sign.y+8*zoom_},{sign.x+36*zoom_,sign.y+8*zoom_},2,neon);
    const char* names[]={"CLINIC","MOTEL","RECORDS","NO SIGNAL"};
    r.Text(sign.x-32*zoom_,sign.y-11*zoom_,names[seed%4],neon,.62f*zoom_);
    if(haunted) {
        Point crack=p(w,d,h*.8f);
        r.Line(crack,p(w-22,d,h*.5f),3,Emissive(Color(214,69,115,alpha),4));
        r.Glow(crack,30*zoom_,Color(214,69,115,alpha));
    }
}
void PrototypeWorld::Player(PrototypeRenderer& r) {
    Point p=Project(0,0);float z=zoom_,step=std::sin(walk_)*3;
    r.Ellipse({p.x,p.y+2*z},20*z,7*z,Color(0,0,0,.65f));
    r.Glow({p.x,p.y-20*z},44*z,Color(64,217,194,.3f));
    r.Line({p.x-5*z,p.y-18*z},{p.x-7*z,p.y-step*z},6*z,Color(12,18,28));
    r.Line({p.x+5*z,p.y-18*z},{p.x+7*z,p.y+step*z},6*z,Color(12,18,28));
    r.Quad({p.x-9*z,p.y-41*z},{p.x+8*z,p.y-41*z},
        {p.x+15*z,p.y-13*z},{p.x-14*z,p.y-15*z},Color(45,63,80));
    r.Line({p.x-7*z,p.y-38*z},{p.x-10*z,p.y-16*z},3*z,Color(85,124,137));
    r.Rect(p.x-7*z,p.y-54*z,14*z,14*z,Color(197,181,170));
    r.Quad({p.x-9*z,p.y-56*z},{p.x+8*z,p.y-58*z},{p.x+10*z,p.y-47*z},
        {p.x-8*z,p.y-48*z},Color(17,22,34));
    r.Rect(p.x-8*z,p.y-42*z,17*z,4*z,Color(174,54,74));
    r.Triangle({p.x-8*z,p.y-41*z},{p.x-28*z,p.y-29*z+std::sin(time_*4)*4*z},
        {p.x-9*z,p.y-34*z},Color(141,38,64));
    if(weapon_) {
        Point hand{p.x+10*z,p.y-29*z};
        float angle=attack_>0?-1.8f+(1-attack_/.45f)*3.6f:-.9f;
        if(facing_.x<0) angle=Pi-angle;
        Point tip{hand.x+std::cos(angle)*65*z,hand.y+std::sin(angle)*65*z};
        r.Glow(tip,25*z,Color(67,245,210));
        r.Line(hand,tip,8*z,Color(20,77,84));
        r.Line(hand,tip,3*z,Emissive(Color(164,255,232),5));
        r.Ellipse(hand,5*z,5*z,Color(239,187,115));
        if(attack_>0) {
            for(int i=0;i<25;++i) {
                float a=angle-i*.045f;
                Point v{hand.x+std::cos(a)*72*z,hand.y+std::sin(a)*72*z};
                r.Line(v,{hand.x+std::cos(a-.04f)*72*z,hand.y+std::sin(a-.04f)*72*z},
                    (8-i*.24f)*z,Emissive(Color(124,255,224,(1-i/25.f)*.8f),4));
            }
        }
    } else {
        Point ghost{p.x+43*z,p.y-44*z+std::sin(time_*3)*6*z};
        r.Ellipse({ghost.x,p.y+5*z},12*z,4*z,Color(0,0,0,.3f));
        r.Glow(ghost,46*z,Color(45,239,202));
        r.Quad({ghost.x,ghost.y-15*z},{ghost.x+13*z,ghost.y},
            {ghost.x,ghost.y+21*z},{ghost.x-13*z,ghost.y},Emissive(Color(84,214,187),2.5f));
        r.Triangle({ghost.x-9*z,ghost.y-5*z},{ghost.x-13*z,ghost.y-23*z},
            {ghost.x-2*z,ghost.y-12*z},Emissive(Color(179,255,225),3));
        r.Triangle({ghost.x+9*z,ghost.y-5*z},{ghost.x+13*z,ghost.y-23*z},
            {ghost.x+2*z,ghost.y-12*z},Emissive(Color(179,255,225),3));
        r.Rect(ghost.x-7*z,ghost.y-3*z,4*z,3*z,Color(8,35,49));
        r.Rect(ghost.x+3*z,ghost.y-3*z,4*z,3*z,Color(8,35,49));
    }
    if(transform_>0) {
        float radius=(1-transform_)*85*z;
        for(int i=0;i<32;++i) {
            float a=i*Pi/16;
            r.Line({p.x+std::cos(a)*radius,p.y-24*z+std::sin(a)*radius*.5f},
                {p.x+std::cos(a+.1f)*radius,p.y-24*z+std::sin(a+.1f)*radius*.5f},
                2*z,Color(145,255,222,transform_));
        }
    }
}
void PrototypeWorld::Rift(PrototypeRenderer& r,float x,float y) {
    Point base=Project(x,y);float z=zoom_;
    Ink red=Emissive(memory_?Color(97,225,193):Color(237,61,103),4);
    r.Ellipse(base,46*z,15*z,Color(101,21,48,.6f));
    r.Glow({base.x,base.y-54*z},100*z,red);
    float wobble=distortion_?std::sin(time_*2)*8:0;
    Point top{base.x+(15+wobble)*z,base.y-129*z};
    r.Quad({base.x-13*z,base.y-10*z},{base.x-18*z,base.y-63*z},top,
        {base.x+17*z,base.y-48*z},Color(5,6,18));
    r.Line({base.x-13*z,base.y-10*z},{base.x-18*z,base.y-63*z},3*z,red);
    r.Line({base.x-18*z,base.y-63*z},top,3*z,red);
    r.Line(top,{base.x+17*z,base.y-48*z},2*z,Emissive(Color(255,161,165),4));
    r.Line({base.x+17*z,base.y-48*z},{base.x-13*z,base.y-10*z},2*z,red);
    for(int i=0;i<14;++i) {
        float t=time_*.25f+i*.42f;
        Point q{base.x+std::sin(t*2)*40*z,base.y-std::fmod(t*30,150.f)*z};
        r.Rect(q.x,q.y,2*z,5*z,Color(255,138,151,.65f));
    }
    r.Text(base.x-40*z,base.y+21*z,memory_?"되찾은 기억":"첫 번째 흔적",red,.7f*z);
}
void PrototypeWorld::Hud(PrototypeRenderer& r) {
    float uiScale=std::min(1.f,std::min(r.Width()/1280.f,r.Height()/800.f));
    r.SetScale(uiScale);
    float w=r.Width()/uiScale,h=r.Height()/uiScale;
    Ink muted=Color(140,159,168),white=Color(224,233,228),cyan=Color(109,234,207),red=Color(245,97,133);
    r.Rect(0,0,w,91,Color(8,15,23,.95f));r.Rect(0,90,w,1,Color(77,104,114,.5f));
    r.Text(28,14,"GSE / RENDER STUDY 01",muted,.7f);
    r.Text(27,36,"THE NAMELESS DISTRICT",white,1.35f);
    r.Text(w-307,19,"SEED 2026 / CITY STREAM",cyan,.72f);
    r.Text(w-307,46,distortion_?"REALITY FRACTURE : ACTIVE":"REALITY FRACTURE : STABLE",red,.72f);
    r.Rect(26,113,300,89,Color(8,17,24,.9f));r.Rect(26,113,3,89,cyan);
    r.Text(43,123,"01 / 잃어버린 이름을 찾아서",cyan,.78f);
    r.Text(43,149,memory_?"균열 속에 목소리가 남아 있었어.":"붉은 균열을 따라가세요.",white,.76f);
    r.Text(43,173,memory_?"기억의 조각을 되찾았습니다.":"가까이 다가가 [E]를 누르세요.",muted,.72f);
    // Resident-chunk minimap; this visualizes real streamed data.
    r.Rect(w-182,113,155,166,Color(8,17,24,.9f));
    r.Text(w-166,124,"LOCAL / 25",muted,.7f);
    for(int y=0;y<5;++y) for(int x=0;x<5;++x) {
        r.Rect(w-161+x*22.f,153+y*21.f,18,17,x==2&&y==2?cyan:Color(38,59,67));
    }
    float foot=h-145;
    r.Rect(26,foot,300,77,Color(8,17,24,.94f));r.Rect(26,foot,3,77,cyan);
    r.Text(43,foot+10,"요괴 / 첫 번째 동료",muted,.7f);
    r.Text(43,foot+34,weapon_?"무기 형태 : 영혼의 검":"동행 중 : 네 곁에 있을게",cyan,.87f);
    r.Rect(345,foot,w-371,77,Color(8,17,24,.94f));
    r.Text(363,foot+9,"동행 요괴 / 대사 미리보기",muted,.68f);
    r.Text(363,foot+32,memory_?"그 약속은 기억나. 내게 부탁했던 사람을 함께 찾자.":
        weapon_?"목소리가 들리지 않아도, 난 여전히 네 곁에 있어.":
        "누군가 네 이름을 기억해 달라고 했어. 저 빛을 따라가 보자.",white,.85f);
    r.Rect(0,h-48,w,48,Color(8,15,23,.97f));
    r.Text(27,h-37,"WASD MOVE   Q TRANSFORM   SPACE SLASH   E INTERACT   V DISTORTION   G CHUNKS   R HOME   ESC EXIT",muted,.69f);
    const PostSettings& post=r.Effects();
    std::ostringstream effects;
    effects<<"H 후처리 "<<(post.enabled?"켜짐":"꺼짐")<<"   B 빛 번짐 "<<(post.bloom?"ON":"OFF")
        <<"   N 비네트 "<<(post.vignette?"ON":"OFF")<<"   M 가장자리 흐림 "<<(post.edgeBlur?"ON":"OFF")
        <<"   [ / ] 노출 "<<int(post.exposure*100+.5f)<<"%   0 초기화";
    r.Text(27,h-18,effects.str(),cyan,.6f);
    if(debug_) {
        std::ostringstream s;s<<"CHUNK "<<player_.cx<<","<<player_.cy<<" | RESIDENT "<<chunks_.size();
        r.Text(32,215,s.str(),cyan,.8f);
    }
    r.SetScale(1);
}
void PrototypeWorld::Draw(PrototypeRenderer& r) {
    zoom_=std::min(r.Width()/1280.f,r.Height()/800.f);
    center_={r.Width()*.48f,r.Height()*.54f};
    struct Item {float x,y,w,d,h,depth;uint32_t seed;int kind;};
    std::vector<Item> items;
    for(const auto& entry:chunks_) {
        float x=float(entry.first.first-player_.cx)*Side-float(player_.x);
        float y=float(entry.first.second-player_.cy)*Side-float(player_.y);
        Floor(r,x,y,entry.second);
        for(int i=0;i<4;++i) {
            float bx=x+175+(i%2)*205,by=y+170+(i/2)*205;
            Point p=Project(bx+72,by+70);
            if(p.x < -250||p.x>r.Width()+250||p.y<0||p.y>r.Height()+350) continue;
            items.push_back({bx,by,145,140,entry.second.heights[i],bx+by+285,entry.second.seed+uint32_t(i),0});
        }
        items.push_back({x+123,y+20,0,0,95,x+y+143,0,1});
    }
    items.push_back({0,0,0,0,0,0,0,2});
    if(player_.cx>=-2&&player_.cx<=2&&player_.cy>=-2&&player_.cy<=2) {
        float x=float(-player_.cx)*Side+RiftX-float(player_.x),y=float(-player_.cy)*Side+RiftY-float(player_.y);
        items.push_back({x,y,0,0,0,x+y,0,3});
    }
    std::sort(items.begin(),items.end(),[](const Item&a,const Item&b){return a.depth<b.depth;});
    for(const Item&i:items) {
        if(i.kind==0) Building(r,i.x,i.y,i.w,i.d,i.h,i.seed);
        else if(i.kind==2) Player(r);
        else if(i.kind==3) Rift(r,i.x,i.y);
        else {
            Point a=Project(i.x,i.y),b=Project(i.x,i.y,95),c{b.x+24*zoom_,b.y};
            r.Ellipse(a,43*zoom_,15*zoom_,Color(79,199,177,.09f));
            r.Line(a,b,3*zoom_,Color(69,90,100));r.Line(b,c,3*zoom_,Color(114,148,146));
            r.Glow(c,65*zoom_,Color(87,218,190,.9f));r.Line(c,{c.x-13*zoom_,c.y},3*zoom_,Emissive(Color(183,251,215),5));
        }
    }
    // Rain is decorative, deterministic and independent of world generation.
    for(int i=0;i<145;++i) {
        float x=std::fmod(float(Mix(i)%10000)+time_*42,float(r.Width()));
        float y=std::fmod(float(Mix(i+150)%10000)+time_*380,float(r.Height()));
        r.Line({x,y},{x-5,y+16},1,Color(117,155,171,.16f));
    }
}
void PrototypeWorld::DrawUI(PrototypeRenderer& r) { Hud(r); }
bool PrototypeWorld::SelfTest() {
    auto require=[](bool ok,const char*name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';return ok;};
    bool ok=true;
    Location negative;negative.x=-1;negative.y=-641;negative.Normalize();
    ok&=require(negative.cx==-1&&negative.cy==-2&&negative.x==639&&negative.y==639,"negative chunk normalization");
    auto a=Generate({-41,9000000000000LL}),b=Generate({-41,9000000000000LL});
    ok&=require(a.seed==b.seed&&a.heights[2]==b.heights[2],"deterministic distant generation");
    PrototypeWorld world;uint32_t first=world.chunks_.at({0,0}).seed;
    world.player_.cx=8000000000000LL;world.Stream();
    ok&=require(world.chunks_.size()==25&&world.chunks_.count({0,0})==0,"bounded resident chunks after travel");
    world.player_=Location{};world.Stream();
    ok&=require(world.chunks_.at({0,0}).seed==first,"return regenerates same chunk");
    Location wall;wall.x=200;wall.y=200;
    ok&=require(world.Blocked(wall)&&!world.Blocked(world.player_),"building collision and clear spawn");
    auto before=world.player_.x;world.Key('d',true);world.Update(.04f);world.Key('d',false);
    ok&=require(world.player_.x!=before,"input movement");
    world.Key('q',true);world.Key('q',true);
    ok&=require(world.weapon_,"transform is edge triggered");world.Key('q',false);
    world.Key(' ',true);world.Key(' ',false);
    ok&=require(world.attack_>0,"weapon slash");
    world.Key('e',true);world.Key('e',false);
    ok&=require(!world.memory_,"interaction range enforced");
    world.player_.x=RiftX;world.player_.y=RiftY;world.Key('e',true);world.Key('e',false);
    ok&=require(world.memory_,"nearby memory interaction");
    return ok;
}
