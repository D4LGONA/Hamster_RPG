using System; using System.Collections.Generic; using System.Globalization;
class P{ static void Main(){ var ci=CultureInfo.InvariantCulture; var sb=new System.Text.StringBuilder();
 for(int cy=-40;cy<=40;++cy) for(int cx=-40;cx<=40;++cx){
  var o=new List<WorldGen.ObstacleSpot>(); WorldGen.ObstaclesInChunk(cx,cy,o); foreach(var s in o) sb.Append($"O {s.TileX} {s.TileY} {(int)s.Kind}\n");
  var m=new List<WorldGen.MonsterSpot>(); WorldGen.MonstersInChunk(cx,cy,m); foreach(var s in m) sb.Append("M "+s.X.ToString("0.0",ci)+" "+s.Y.ToString("0.0",ci)+" "+(int)s.Kind+"\n");}
 for(float x=-30.3f;x<30;x+=0.7f) for(float y=-30.1f;y<30;y+=0.9f) sb.Append("B "+(WorldGen.IsBlocked(x,y,0.4f)?1:0)+"\n");
 Console.Write(sb.ToString()); }}
