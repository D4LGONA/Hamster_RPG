#include "../../Server/WorldGen.h"
#include <cstdio>
int main(){ using namespace WorldGen;
 for(int cy=-40;cy<=40;++cy) for(int cx=-40;cx<=40;++cx){
  std::vector<ObstacleSpot> o; ObstaclesInChunk(cx,cy,o); for(auto&s:o) printf("O %d %d %d\n",s.tileX,s.tileY,(int)s.kind);
  std::vector<MonsterSpot> m; MonstersInChunk(cx,cy,m); for(auto&s:m) printf("M %.1f %.1f %d\n",s.x,s.y,(int)s.kind);}
 for(float x=-30.3f;x<30;x+=0.7f) for(float y=-30.1f;y<30;y+=0.9f) printf("B %d\n",(int)IsBlocked(x,y,0.4f));
}
