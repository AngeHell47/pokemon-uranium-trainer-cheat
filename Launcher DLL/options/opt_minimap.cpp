// opt_minimap.cpp - Minimap RGSS pour Pokemon Uranium.
// Elle est rendue dans RGSS : pas de capture du backbuffer ni d'appel Ruby
// depuis le thread de l'overlay Windows.

#include "opt_minimap.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_minimap_enabled = true;
bool g_minimap_round   = false;
bool g_minimap_show_fps = false;
int  g_minimap_size    = 128;
int  g_minimap_zoom    = 100;

static char s_ini[MAX_PATH] = {};
static volatile LONG s_enabled = 1;
static volatile LONG s_round = 0;
static volatile LONG s_show_fps = 0;
static volatile LONG s_size = 128;
static volatile LONG s_zoom = 100;
static volatile LONG s_pending = 1;
static DWORD s_last_install_attempt = 0;

static int clamp_value(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void request_update() {
    InterlockedExchange(&s_pending, 1);
    rgss_safe_dispatch_notify();
}

// Ruby 1.8 compatible. Les IDs de tuiles et leur praticabilite produisent une
// carte lisible (murs/routes/eau/sol) sans charger les bitmaps de tilesets a
// chaque frame.
static const char kMiniMapRuby[] =
"begin\n"
"  unless defined?(::UraniumMiniMap)\n"
"    class ::UraniumMiniMap\n"
"      class << self\n"
"        def configure(enabled,size,zoom,rounded,show_fps)\n"
"          changed=(@size.to_i!=size.to_i || @zoom.to_i!=zoom.to_i || @rounded!=(rounded.to_i!=0))\n"
"          @enabled=(enabled.to_i!=0)\n"
"          @size=[[size.to_i,64].max,256].min\n"
"          @zoom=[[zoom.to_i,25].max,400].min\n"
"          @rounded=(rounded.to_i!=0)\n"
"          @show_fps=(show_fps.to_i!=0)\n"
"          @dirty=true if changed || @dirty.nil?\n"
"        end\n"
"        def ensure_sprite\n"
"          return if @sprite && !@sprite.disposed? && @bitmap && !@bitmap.disposed? && @bitmap.width==@size\n"
"          dispose\n"
"          @bitmap=Bitmap.new(@size,@size)\n"
"          @sprite=Sprite.new\n"
"          @sprite.bitmap=@bitmap\n"
"          @sprite.z=999999\n"
"          @dirty=true\n"
"        end\n"
"        def dispose\n"
"          @sprite.dispose if @sprite && !@sprite.disposed?\n"
"          @bitmap.dispose if @bitmap && !@bitmap.disposed?\n"
"          @fps_sprite.dispose if @fps_sprite && !@fps_sprite.disposed?\n"
"          @fps_bitmap.dispose if @fps_bitmap && !@fps_bitmap.disposed?\n"
"          @map_cache.dispose if @map_cache && !@map_cache.disposed?\n"
"          @sprite=nil\n"
"          @bitmap=nil\n"
"          @fps_sprite=nil\n"
"          @fps_bitmap=nil\n"
"          @map_cache=nil\n"
"        rescue Exception\n"
"          @sprite=nil\n"
"          @bitmap=nil\n"
"          @fps_sprite=nil\n"
"          @fps_bitmap=nil\n"
"          @map_cache=nil\n"
"        end\n"
"        def tile_id(map,x,y)\n"
"          return 0 if x<0 || y<0 || x>=map.width || y>=map.height\n"
"          value=0\n"
"          data=(map.respond_to?(:data) ? map.data : map.instance_variable_get(:@map).data)\n"
"          2.downto(0) do |z|\n"
"            t=data[x,y,z].to_i\n"
"            if t>0\n"
"              value=t\n"
"              break\n"
"            end\n"
"          end\n"
"          return value\n"
"        rescue Exception\n"
"          return 0\n"
"        end\n"
"        def tileset_for(map)\n"
"          return nil if !defined?($data_tilesets) || !$data_tilesets\n"
"          tid=(map.respond_to?(:tileset_id) ? map.tileset_id : map.instance_variable_get(:@map).tileset_id)\n"
"          return $data_tilesets[tid]\n"
"        rescue Exception\n"
"          return nil\n"
"        end\n"
"        def bitmap_color(map,id,quadrant)\n"
"          return nil if id<48\n"
"          ts=tileset_for(map)\n"
"          return nil if !ts\n"
"          bmp=nil\n"
"          x=16\n"
"          y=16\n"
"          if id>=384\n"
"            bmp=RPG::Cache.tileset(ts.tileset_name)\n"
"            index=id-384\n"
"            x=index.modulo(8)*32+(quadrant.modulo(2)==0 ? 8 : 24)\n"
"            y=(index/8)*32+(quadrant<2 ? 8 : 24)\n"
"          else\n"
"            names=ts.autotile_names\n"
"            name=names[id/48-1]\n"
"            return nil if !name || name==\"\"\n"
"            bmp=RPG::Cache.autotile(name)\n"
"            x=bmp.width/2+(quadrant.modulo(2)==0 ? -8 : 8)\n"
"            y=bmp.height/2+(quadrant<2 ? -8 : 8)\n"
"          end\n"
"          return nil if !bmp || bmp.disposed?\n"
"          samples=[[x,y],[x-3,y-3],[x+3,y-3],[x-3,y+3],[x+3,y+3]]\n"
"          red=green=blue=count=0\n"
"          samples.each do |p|\n"
"            next if p[0]<0 || p[1]<0 || p[0]>=bmp.width || p[1]>=bmp.height\n"
"            c=bmp.get_pixel(p[0],p[1])\n"
"            next if c.alpha<24\n"
"            red+=c.red\n"
"            green+=c.green\n"
"            blue+=c.blue\n"
"            count+=1\n"
"          end\n"
"          return nil if count==0\n"
"          return Color.new(red/count,green/count,blue/count,245)\n"
"        rescue Exception\n"
"          return nil\n"
"        end\n"
"        def tile_color(map,id,quadrant=0)\n"
"          return Color.new(20,24,34,230) if id<=0\n"
"          if @color_map_id!=map.map_id\n"
"            @color_map_id=map.map_id\n"
"            @tile_colors={}\n"
"          end\n"
"          key=id*4+quadrant\n"
"          cached=@tile_colors[key] if @tile_colors\n"
"          return cached if cached\n"
"          blocked=false\n"
"          begin\n"
"            blocked=(map.passages[id].to_i&15)!=0\n"
"          rescue Exception\n"
"          end\n"
"          color=bitmap_color(map,id,quadrant)\n"
"          color=Color.new(60+(id*13).modulo(35),76+(id*19).modulo(45),54+(id*7).modulo(32),245) if !color\n"
"          if blocked\n"
"            color=Color.new((color.red*0.58).to_i,(color.green*0.58).to_i,(color.blue*0.62).to_i,245)\n"
"          end\n"
"          @tile_colors[key]=color\n"
"          return color\n"
"        end\n"
"        def build_map_cache(map)\n"
"          width=map.width.to_i\n"
"          height=map.height.to_i\n"
"          return false if width<=0 || height<=0\n"
"          detail=(width*2<=2048 && height*2<=2048) ? 2 : 1\n"
"          if @map_cache && !@map_cache.disposed? && @cache_map_id==map.map_id && @cache_width==width && @cache_height==height && @cache_detail==detail\n"
"            return true\n"
"          end\n"
"          @map_cache.dispose if @map_cache && !@map_cache.disposed?\n"
"          @map_cache=Bitmap.new(width*detail,height*detail)\n"
"          height.times do |y|\n"
"            width.times do |x|\n"
"              id=tile_id(map,x,y)\n"
"              if detail==2\n"
"                @map_cache.set_pixel(x*2,y*2,tile_color(map,id,0))\n"
"                @map_cache.set_pixel(x*2+1,y*2,tile_color(map,id,1))\n"
"                @map_cache.set_pixel(x*2,y*2+1,tile_color(map,id,2))\n"
"                @map_cache.set_pixel(x*2+1,y*2+1,tile_color(map,id,3))\n"
"              else\n"
"                @map_cache.set_pixel(x,y,tile_color(map,id,0))\n"
"              end\n"
"            end\n"
"          end\n"
"          @cache_map_id=map.map_id\n"
"          @cache_width=width\n"
"          @cache_height=height\n"
"          @cache_detail=detail\n"
"          return true\n"
"        rescue Exception\n"
"          @map_cache=nil\n"
"          return false\n"
"        end\n"
"        def inside?(x,y)\n"
"          return true if !@rounded\n"
"          d=@size.to_f/2.0\n"
"          dx=x-d\n"
"          dy=y-d\n"
"          return dx*dx+dy*dy<=d*d\n"
"        end\n"
"        def paint\n"
"          return if !$game_map || !$game_player || !@bitmap\n"
"          map=$game_map\n"
"          return if !build_map_cache(map)\n"
"          @bitmap.fill_rect(0,0,@size,@size,Color.new(10,14,22,225))\n"
"          scale=[(@zoom.to_f/50.0),0.5].max\n"
"          visible=@size.to_f/scale\n"
"          left=$game_player.x.to_f+0.5-visible/2.0\n"
"          top=$game_player.y.to_f+0.5-visible/2.0\n"
"          right=left+visible\n"
"          bottom=top+visible\n"
"          clip_left=[left,0.0].max\n"
"          clip_top=[top,0.0].max\n"
"          clip_right=[right,map.width.to_f].min\n"
"          clip_bottom=[bottom,map.height.to_f].min\n"
"          if clip_right>clip_left && clip_bottom>clip_top\n"
"            dx=((clip_left-left)*@size/visible).round\n"
"            dy=((clip_top-top)*@size/visible).round\n"
"            dw=[((clip_right-clip_left)*@size/visible).round,1].max\n"
"            dh=[((clip_bottom-clip_top)*@size/visible).round,1].max\n"
"            detail=@cache_detail\n"
"            sx=(clip_left*detail).floor\n"
"            sy=(clip_top*detail).floor\n"
"            sw=[((clip_right-clip_left)*detail).ceil,1].max\n"
"            sh=[((clip_bottom-clip_top)*detail).ceil,1].max\n"
"            @bitmap.stretch_blt(Rect.new(dx,dy,dw,dh),@map_cache,Rect.new(sx,sy,sw,sh))\n"
"          end\n"
"          if @rounded\n"
"            radius=@size.to_f/2.0\n"
"            @size.times do |row|\n"
"              vertical=(row+0.5)-radius\n"
"              span=Math.sqrt([radius*radius-vertical*vertical,0.0].max)\n"
"              first=(radius-span).floor\n"
"              last=(radius+span).ceil\n"
"              @bitmap.clear_rect(0,row,first,1) if first>0\n"
"              @bitmap.clear_rect(last,row,@size-last,1) if last<@size\n"
"            end\n"
"          end\n"
"          cx=@size/2\n"
"          cy=@size/2\n"
"          pin=Color.new(255,66,66,255)\n"
"          edge=Color.new(255,255,255,255)\n"
"          @bitmap.fill_rect(cx-4,cy-1,9,3,edge)\n"
"          @bitmap.fill_rect(cx-1,cy-4,3,9,edge)\n"
"          @bitmap.fill_rect(cx-2,cy,5,1,pin)\n"
"          @bitmap.fill_rect(cx,cy-2,1,5,pin)\n"
"          @last_map=map.map_id\n"
"          @last_x=$game_player.x\n"
"          @last_y=$game_player.y\n"
"          @last_paint=Graphics.frame_count\n"
"          @dirty=false\n"
"        rescue Exception\n"
"          @dirty=true\n"
"        end\n"
"        def ensure_fps\n"
"          return if @fps_sprite && !@fps_sprite.disposed? && @fps_bitmap && !@fps_bitmap.disposed?\n"
"          @fps_bitmap=Bitmap.new(78,20)\n"
"          @fps_sprite=Sprite.new\n"
"          @fps_sprite.bitmap=@fps_bitmap\n"
"          @fps_sprite.z=1000000\n"
"          @fps_clock=Win32API.new(\"kernel32\",\"GetTickCount\",[],\"l\")\n"
"          @fps_last_tick=@fps_clock.call\n"
"          @fps_last_frame=render_frame_count\n"
"          @fps_value=0\n"
"        end\n"
"        def render_frame_count\n"
"          if Graphics.respond_to?(:__uranium_trainer_original_frame_count)\n"
"            return Graphics.__uranium_trainer_original_frame_count.to_i\n"
"          end\n"
"          return Graphics.frame_count.to_i\n"
"        rescue Exception\n"
"          return Graphics.frame_count.to_i\n"
"        end\n"
"        def update_fps\n"
"          if !@show_fps\n"
"            @fps_sprite.visible=false if @fps_sprite && !@fps_sprite.disposed?\n"
"            return\n"
"          end\n"
"          ensure_fps\n"
"          now=@fps_clock.call\n"
"          elapsed=now-@fps_last_tick\n"
"          if elapsed>=500\n"
"            current_frame=render_frame_count\n"
"            frame_delta=current_frame-@fps_last_frame\n"
"            frame_delta=0 if frame_delta<0\n"
"            @fps_value=((frame_delta*1000.0)/elapsed).round\n"
"            @fps_last_frame=current_frame\n"
"            @fps_last_tick=now\n"
"            @fps_bitmap.clear\n"
"            @fps_bitmap.fill_rect(0,0,78,20,Color.new(8,12,20,185))\n"
"            @fps_bitmap.draw_text(0,0,78,20,\"FPS #{@fps_value}\",1)\n"
"          end\n"
"          @fps_sprite.x=12\n"
"          @fps_sprite.y=12\n"
"          @fps_sprite.visible=true\n"
"        rescue Exception\n"
"        end\n"
"        def update\n"
"          update_fps\n"
"          if !@enabled\n"
"            @sprite.visible=false if @sprite && !@sprite.disposed?\n"
"            return\n"
"          end\n"
"          return if !$scene || !defined?(Scene_Map) || !$scene.is_a?(Scene_Map)\n"
"          ensure_sprite\n"
"          @sprite.visible=true\n"
"          @sprite.x=Graphics.width-@size-12\n"
"          @sprite.y=12\n"
"          now=Graphics.frame_count\n"
"          moved=(!$game_player || @last_x!=$game_player.x || @last_y!=$game_player.y || @last_map!=$game_map.map_id)\n"
"          paint if @dirty || (moved && now.to_i-(@last_paint || 0).to_i>=3)\n"
"        rescue Exception\n"
"        end\n"
"        def hide\n"
"          @sprite.visible=false if @sprite && !@sprite.disposed?\n"
"          @fps_sprite.visible=false if @fps_sprite && !@fps_sprite.disposed?\n"
"        rescue Exception\n"
"        end\n"
"      end\n"
"    end\n"
"    if defined?(::Scene_Map) && !::Scene_Map.method_defined?(:__uranium_minimap_update_v1)\n"
"      class ::Scene_Map\n"
"        alias __uranium_minimap_update_v1 update\n"
"        def update\n"
"          __uranium_minimap_update_v1\n"
"          ::UraniumMiniMap.update\n"
"        end\n"
"        if method_defined?(:main)\n"
"          alias __uranium_minimap_main_v1 main\n"
"          def main\n"
"            begin\n"
"              __uranium_minimap_main_v1\n"
"            ensure\n"
"              ::UraniumMiniMap.hide\n"
"            end\n"
"          end\n"
"        end\n"
"      end\n"
"    end\n"
"  end\n"
"  ::UraniumMiniMap.configure(%d,%d,%d,%d,%d) if defined?(::UraniumMiniMap)\n"
"rescue Exception\n"
"end\n";

static void __cdecl on_game_thread_tick(void*) {
    const DWORD now = GetTickCount();
    const bool pending = InterlockedExchange(&s_pending, 0) != 0;
    // Scene_Map peut ne pas etre charge au premier safe point : le bootstrap
    // est idempotent et retente periodiquement jusqu'a ce qu'il le soit.
    if (!pending && s_last_install_attempt != 0 && now - s_last_install_attempt < 1000)
        return;
    s_last_install_attempt = now;
    char ruby[sizeof(kMiniMapRuby) + 96] = {};
    _snprintf_s(ruby, sizeof(ruby), _TRUNCATE, kMiniMapRuby,
                (int)InterlockedCompareExchange(&s_enabled, 0, 0),
                (int)InterlockedCompareExchange(&s_size, 0, 0),
                (int)InterlockedCompareExchange(&s_zoom, 0, 0),
                (int)InterlockedCompareExchange(&s_round, 0, 0),
                (int)InterlockedCompareExchange(&s_show_fps, 0, 0));
    if (rgss_safe_eval(ruby) != 0) InterlockedExchange(&s_pending, 1);
}

void opt_minimap_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_minimap_enabled=GetPrivateProfileIntA("Settings","MiniMapEnabled",1,s_ini)!=0;
    g_minimap_round=GetPrivateProfileIntA("Settings","MiniMapRound",0,s_ini)!=0;
    g_minimap_show_fps=GetPrivateProfileIntA("Settings","MiniMapShowFps",0,s_ini)!=0;
    g_minimap_size=clamp_value(GetPrivateProfileIntA("Settings","MiniMapSize",128,s_ini),OPT_MINIMAP_MIN_SIZE,OPT_MINIMAP_MAX_SIZE);
    g_minimap_zoom=clamp_value(GetPrivateProfileIntA("Settings","MiniMapZoom",100,s_ini),OPT_MINIMAP_MIN_ZOOM,OPT_MINIMAP_MAX_ZOOM);
    InterlockedExchange(&s_enabled,g_minimap_enabled?1:0);
    InterlockedExchange(&s_round,g_minimap_round?1:0);
    InterlockedExchange(&s_show_fps,g_minimap_show_fps?1:0);
    InterlockedExchange(&s_size,g_minimap_size);
    InterlockedExchange(&s_zoom,g_minimap_zoom);
    InterlockedExchange(&s_pending,1);
}

void opt_minimap_set_hwnd_and_start(HWND hwnd) {
    (void)hwnd;
    rgss_safe_dispatch_register(on_game_thread_tick,NULL);
    request_update();
}

void opt_minimap_toggle(bool enabled) {
    g_minimap_enabled=enabled;
    InterlockedExchange(&s_enabled,enabled?1:0);
    WritePrivateProfileStringA("Settings","MiniMapEnabled",enabled?"1":"0",s_ini);
    request_update();
}

void opt_minimap_set_round(bool rounded) {
    g_minimap_round=rounded;
    InterlockedExchange(&s_round,rounded?1:0);
    WritePrivateProfileStringA("Settings","MiniMapRound",rounded?"1":"0",s_ini);
    request_update();
}

void opt_minimap_toggle_fps(bool enabled) {
    g_minimap_show_fps=enabled;
    InterlockedExchange(&s_show_fps,enabled?1:0);
    WritePrivateProfileStringA("Settings","MiniMapShowFps",enabled?"1":"0",s_ini);
    request_update();
}

void opt_minimap_set_size(int pixels) {
    g_minimap_size=clamp_value(pixels,OPT_MINIMAP_MIN_SIZE,OPT_MINIMAP_MAX_SIZE);
    InterlockedExchange(&s_size,g_minimap_size);
    char value[16]; sprintf_s(value,sizeof(value),"%d",g_minimap_size);
    WritePrivateProfileStringA("Settings","MiniMapSize",value,s_ini);
    request_update();
}

void opt_minimap_set_zoom(int percent) {
    g_minimap_zoom=clamp_value(percent,OPT_MINIMAP_MIN_ZOOM,OPT_MINIMAP_MAX_ZOOM);
    InterlockedExchange(&s_zoom,g_minimap_zoom);
    char value[16]; sprintf_s(value,sizeof(value),"%d",g_minimap_zoom);
    WritePrivateProfileStringA("Settings","MiniMapZoom",value,s_ini);
    request_update();
}
