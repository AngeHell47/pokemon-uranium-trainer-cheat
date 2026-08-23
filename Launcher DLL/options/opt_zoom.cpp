// opt_zoom.cpp - Dezoom de carte RGSS sans agrandir la fenetre.
//
// Le jeu possede deja toute la plomberie necessaire pour changer d'echelle :
// Graphics.width/height sont les dimensions logiques et pbSetResizeFactor2
// adapte les Sprite/Viewport/Plane ainsi que la surface RGSS.  Le trainer
// agrandit donc uniquement les dimensions logiques et applique le facteur
// inverse.  Aucun hook ni aucune capture GDI ne sont necessaires.

#include "../options/opt_zoom.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string>

int g_zoom_value = 100;

volatile OptZoomTelemetry g_zoom_telemetry = {0};
static_assert(sizeof(OptZoomTelemetry) == 36,
              "Le contrat de telemetrie Ruby doit rester compose de 9 LONG.");

static char          s_ini[MAX_PATH] = {0};
static HWND          s_game_hwnd = NULL;
static int           s_base_client_w = 0;
static int           s_base_client_h = 0;
static DWORD         s_last_tick = 0;
static DWORD         s_last_install_attempt = 0;
static DWORD         s_client_candidate_since = 0;
static int           s_client_candidate_w = 0;
static int           s_client_candidate_h = 0;
static volatile LONG s_requested_percent = 100;

enum ZoomError {
    ZOOM_OK                    = 0,
    ZOOM_ERR_NOT_READY         = 3,
    ZOOM_ERR_APPLY             = 4,
    ZOOM_ERR_RESTORE           = 5,
    ZOOM_ERR_RECREATE          = 6,
    ZOOM_ERR_CLIENT_SIZE       = 7,
    ZOOM_ERR_ASPECT            = 8,
    ZOOM_ERR_INSTALL           = 9,
    ZOOM_ERR_NATIVE_EVAL       = 10
};

// Ruby 1.8 compatible.  The patch is deliberately self-contained and
// idempotent because it can be evaluated while the title screen is still up.
// Its one-shot evaluation happens at the native Graphics.update Ruby boundary.
static const char s_patch_body[] =
"begin\n"
"  $__uranium_camera_copy = Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
"  $__uranium_camera_read = Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"p\",\"l\",\"l\"],\"v\")\n"
"  ::Graphics.module_eval(\"def self.dll_camera_set_size(w,h)\\n\"+\n"
"     \"  @@width=w.to_i\\n  @@height=h.to_i\\nend\\n\")\n"
  "  if !(defined?($__uranium_camera_patch_v8_fixed500) && $__uranium_camera_patch_v8_fixed500)\n"
"    module ::UraniumCamera\n"
"      class << self\n"
"        def install(native_address,requested_address,client_w,client_h)\n"
"          @native_address=native_address.to_i\n"
"          @requested_address=requested_address.to_i\n"
"          @client_w=client_w.to_i\n"
"          @client_h=client_h.to_i\n"
"          @base_w=Graphics.width.to_i\n"
"          @base_h=Graphics.height.to_i\n"
"          @base_factor=($ResizeFactor || 1.0).to_f\n"
"          @base_mul=($ResizeFactorMul || (@base_factor*100.0))\n"
"          @base_offset_x=($ResizeOffsetX || 0).to_f\n"
"          @base_offset_y=($ResizeOffsetY || 0).to_f\n"
"          @target_pct=100\n"
"          @applied_pct=100\n"
"          @logical_w=@base_w\n"
"          @logical_h=@base_h\n"
"          @tile_bleed=false\n"
"          @suspend_depth=0\n"
"          @error=0\n"
"          @changing=false\n"
"          begin\n"
"            fade=Graphics.module_eval(\"@@fadeoutvp\")\n"
"            rect=fade.rect\n"
"            @fade_viewport=fade\n"
"            @base_fade_rect=[rect.x,rect.y,rect.width,rect.height]\n"
"          rescue Exception\n"
"            @fade_viewport=nil\n"
"            @base_fade_rect=nil\n"
"          end\n"
"          @gcd=gcd(@base_w,@base_h)\n"
"          @ratio_w=@base_w/@gcd\n"
"          @ratio_h=@base_h/@gcd\n"
"          aspect_error=(@client_w*@base_h-@client_h*@base_w).abs\n"
"          aspect_tolerance=[@base_w,@base_h].max*2\n"
"          @aspect_valid=(aspect_error<=aspect_tolerance)\n"
"          @error=8 if !@aspect_valid\n"
"          telemetry\n"
"        end\n"
"\n"
"        def initialized?\n"
"          return @base_w.to_i>0 && @base_h.to_i>0\n"
"        end\n"
"\n"
"        def active?\n"
"          return @applied_pct.to_i>100\n"
"        end\n"
"\n"
"        def update_endpoint(native_address,requested_address,client_w,client_h)\n"
"          @native_address=native_address.to_i\n"
"          @requested_address=requested_address.to_i\n"
"          @client_w=client_w.to_i if client_w.to_i>0\n"
"          @client_h=client_h.to_i if client_h.to_i>0\n"
"          @tile_bleed=(@logical_w.to_i!=@base_w.to_i ||\n"
"             @logical_h.to_i!=@base_h.to_i)\n"
"          telemetry\n"
"        end\n"
"\n"
"        def update_client(client_w,client_h)\n"
"          @client_w=client_w.to_i if client_w.to_i>0\n"
"          @client_h=client_h.to_i if client_h.to_i>0\n"
"          telemetry\n"
"        end\n"
"\n"
"        def gcd(a,b)\n"
"          a=a.abs\n"
"          b=b.abs\n"
"          while b!=0\n"
"            t=a%b\n"
"            a=b\n"
"            b=t\n"
"          end\n"
"          return a>0 ? a : 1\n"
"        end\n"
"\n"
"        def telemetry(error=nil)\n"
"          @error=error.to_i if !error.nil?\n"
"          values=[1,(@applied_pct || 100),(@base_w || 0),(@base_h || 0),\n"
"             (@logical_w || 0),(@logical_h || 0),(@client_w || 0),\n"
"             (@client_h || 0),(@error || 0)]\n"
"          $__uranium_camera_copy.call(@native_address,values.pack(\"l*\"),36) if @native_address && @native_address!=0\n"
"        rescue Exception\n"
"        end\n"
"\n"
"        def clamp_percent(percent)\n"
"          percent=percent.to_i\n"
"          percent=100 if percent<100\n"
"          percent=500 if percent>500\n"
"          return percent\n"
"        end\n"
"\n"
"        # The overlay may run on another native thread. It only publishes an\n"
"        # integer here; the RGSS thread consumes it from Scene_Map#update.\n"
"        # This avoids calling RGSSEval recursively from a Windows hook each\n"
"        # time the slider is committed.\n"
"        def requested_percent\n"
"          return clamp_percent(@target_pct || 100) if !@requested_address ||\n"
"             @requested_address==0\n"
"          buffer=(@request_buffer ||= [0].pack(\"l\"))\n"
"          $__uranium_camera_read.call(buffer,@requested_address,4)\n"
"          return clamp_percent(buffer.unpack(\"l\")[0])\n"
"        rescue Exception\n"
"          return clamp_percent(@target_pct || 100)\n"
"        end\n"
"\n"
"        def stable_map?(scene,need_spritesets=true)\n"
// stable_map? est appelee depuis Scene_Map#update ou avec $scene, puis valide
// deja les objets de carte et @spritesets. Le test is_a? par image etait donc
// redondant et traversait exactement la routine RGSS du crash C0000005.
"          return false if !scene\n"
"          return false if !$game_player || !$game_map\n"
"          return false if !Graphics.respond_to?(:dll_camera_set_size)\n"
"          return false if !Object.private_method_defined?(:pbSetResizeFactor2) &&\n"
"                          !Object.method_defined?(:pbSetResizeFactor2)\n"
"          if need_spritesets\n"
"            sets=scene.instance_variable_get(:@spritesets)\n"
"            return false if !sets || !sets.is_a?(Hash) || sets.empty?\n"
"          end\n"
"          return true\n"
"        rescue Exception\n"
"          return false\n"
"        end\n"
"\n"
"        def dimensions_for(percent)\n"
"          units=((@base_w/@ratio_w).to_f*percent.to_f/100.0).round\n"
"          units=1 if units<1\n"
"          return [@ratio_w*units,@ratio_h*units]\n"
"        end\n"
"\n"
"        def reflow_objects\n"
"          ObjectSpace.each_object(Sprite) do |o|\n"
"            begin\n"
"              next if o.disposed?\n"
"              o.x=o.x; o.y=o.y; o.ox=o.ox; o.oy=o.oy\n"
"              o.zoom_x=o.zoom_x; o.zoom_y=o.zoom_y\n"
"            rescue Exception\n"
"            end\n"
"          end\n"
"          ObjectSpace.each_object(Viewport) do |o|\n"
"            begin\n"
"              o.rect=o.rect; o.ox=o.ox; o.oy=o.oy\n"
"            rescue Exception\n"
"            end\n"
"          end\n"
"          ObjectSpace.each_object(Plane) do |o|\n"
"            begin\n"
"              next if o.disposed?\n"
"              o.zoom_x=o.zoom_x; o.zoom_y=o.zoom_y\n"
"            rescue Exception\n"
"            end\n"
"          end\n"
"        end\n"
"\n"
"        # CustomTilemap dessine les tuiles de priorite avec un Sprite par\n"
"        # case. A une echelle fractionnaire, Sprite_Resizer tronque chaque\n"
"        # position et RGSS arrondit chaque largeur independamment. Les deux\n"
"        # arrondis divergent periodiquement et laissent un pixel transparent.\n"
"        # On calcule donc la largeur physique depuis les memes bornes que les\n"
"        # positions. Deux tuiles voisines partagent alors exactement la meme\n"
"        # frontiere, meme pour les coordonnees negatives pendant un scroll.\n"
"        def fit_tile_sprite(sprite,x,y,width,height)\n"
"          return if !sprite || sprite.disposed?\n"
"          return if !@tile_bleed\n"
"          factor=($ResizeFactorMul || 100).to_f/100.0\n"
"          width=width.to_i\n"
"          height=height.to_i\n"
"          return if factor<=0.0 || width<=0 || height<=0\n"
"          return if !sprite.respond_to?(:_xeq_SpriteResizer) ||\n"
"                    !sprite.respond_to?(:_yeq_SpriteResizer) ||\n"
"                    !sprite.respond_to?(:_zoomxeq_SpriteResizer) ||\n"
"                    !sprite.respond_to?(:_zoomyeq_SpriteResizer)\n"
"          left=(x.to_f*factor).to_i\n"
"          top=(y.to_f*factor).to_i\n"
"          # RGSS laisse parfois le dernier texel transparent malgre une\n"
"          # largeur mathematiquement suffisante. Un pixel physique de bleed\n"
"          # recouvre cette couture sans modifier la geometrie logique.\n"
"          # Elle reste donc valide quand le fast path translate le sprite,\n"
"          # sans recalculer des milliers de zooms a chaque frame de marche.\n"
"          pixel_w=[(width.to_f*factor).ceil+1,1].max\n"
"          pixel_h=[(height.to_f*factor).ceil+1,1].max\n"
"          sprite._xeq_SpriteResizer(left)\n"
"          sprite._yeq_SpriteResizer(top)\n"
"          sprite._zoomxeq_SpriteResizer(pixel_w.to_f/width.to_f)\n"
"          sprite._zoomyeq_SpriteResizer(pixel_h.to_f/height.to_f)\n"
"        rescue Exception\n"
"        end\n"
"\n"
"        # Le moteur utilise Graphics.width/height pour decider quels PNJ\n"
"        # doivent etre simules et redessines. A 500 %, cela activait toute la\n"
"        # carte et ralentissait aussi la logique de jeu. On garde une zone\n"
"        # active equivalente a l'ecran vanilla et sa bordure de 6 tuiles ;\n"
"        # lointain reste visible, mais ses PNJ attendent d'etre approches.\n"
"        def event_in_update_range?(map,object)\n"
"          return true if @applied_pct.to_i<=100 || !map || !object\n"
"          return true if defined?($PokemonSystem) && $PokemonSystem &&\n"
"             $PokemonSystem.tilemap==2\n"
"          sub_x=Game_Map::XSUBPIXEL\n"
"          sub_y=Game_Map::YSUBPIXEL\n"
"          if $game_player && $game_map\n"
"            center_x=map.display_x+($game_player.real_x-$game_map.display_x)\n"
"            center_y=map.display_y+($game_player.real_y-$game_map.display_y)\n"
"          else\n"
"            center_x=map.display_x+(Graphics.width*sub_x/2)\n"
"            center_y=map.display_y+(Graphics.height*sub_y/2)\n"
"          end\n"
"          radius_x=((@base_w || 512)/2+192)*sub_x\n"
"          radius_y=((@base_h || 384)/2+192)*sub_y\n"
"          return false if object.real_x<=center_x-radius_x ||\n"
"             object.real_x>=center_x+radius_x\n"
"          return false if object.real_y<=center_y-radius_y ||\n"
"             object.real_y>=center_y+radius_y\n"
"          return true\n"
"        rescue Exception\n"
"          return true\n"
"        end\n"
"\n"
"        def recenter\n"
"          $game_player.center($game_player.x,$game_player.y) if $game_player\n"
"        rescue Exception\n"
"        end\n"
"\n"
"        def resize_fade_viewport(logical_w,logical_h)\n"
"          return if !@fade_viewport\n"
"          if logical_w.to_i==@base_w.to_i && logical_h.to_i==@base_h.to_i &&\n"
"             @base_fade_rect\n"
"            @fade_viewport.rect.set(@base_fade_rect[0],@base_fade_rect[1],\n"
"               @base_fade_rect[2],@base_fade_rect[3])\n"
"          else\n"
"            @fade_viewport.rect.set(0,0,logical_w.to_i,logical_h.to_i)\n"
"          end\n"
"        rescue Exception\n"
"        end\n"
"\n"
"        def recreate(scene)\n"
"          return true if !scene\n"
"          scene.disposeSpritesets\n"
"          scene.createSpritesets\n"
"          return true\n"
"        rescue Exception\n"
"          begin\n"
"            sets=scene.instance_variable_get(:@spritesets)\n"
"            scene.createSpritesets if !sets || sets.empty?\n"
"          rescue Exception\n"
"          end\n"
"          return false\n"
"        end\n"
"\n"
"        def set_scale(logical_w,logical_h,factor,mul,offset_x,offset_y)\n"
"          @tile_bleed=(logical_w.to_i!=@base_w.to_i ||\n"
"             logical_h.to_i!=@base_h.to_i)\n"
"          Graphics.dll_camera_set_size(logical_w,logical_h)\n"
"          $ResizeOffsetX=offset_x\n"
"          $ResizeOffsetY=offset_y\n"
"          pbSetResizeFactor2(factor,true)\n"
"          $ResizeFactorMul=mul\n"
"          reflow_objects\n"
"          resize_fade_viewport(logical_w,logical_h)\n"
"          @logical_w=logical_w.to_i\n"
"          @logical_h=logical_h.to_i\n"
"        end\n"
"\n"
"        def restore(scene=nil,recreate_sprites=true)\n"
"          return true if @changing\n"
"          @changing=true\n"
"          begin\n"
"            set_scale(@base_w,@base_h,@base_factor,@base_mul,\n"
"               @base_offset_x,@base_offset_y)\n"
"            recenter\n"
"            ok=(!recreate_sprites || recreate(scene))\n"
"            @applied_pct=100\n"
"            @error=(ok ? 0 : 6)\n"
"            telemetry\n"
"            return ok\n"
"          rescue Exception\n"
"            # Conserver l'etat precedent : pump retentera la restauration au\n"
"            # prochain frame au lieu de publier a tort un retour a 100 %.\n"
"            telemetry(5)\n"
"            return false\n"
"          ensure\n"
"            @changing=false\n"
"          end\n"
"        end\n"
"\n"
"        def apply(scene,percent,recreate_sprites=true)\n"
"          percent=clamp_percent(percent)\n"
"          return restore(scene,recreate_sprites) if percent==100\n"
"          if !@aspect_valid\n"
"            telemetry(8)\n"
"            return false\n"
"          end\n"
"          if !stable_map?(scene,recreate_sprites)\n"
"            telemetry(3)\n"
"            return false\n"
"          end\n"
"          return false if @changing\n"
"          @changing=true\n"
"          begin\n"
"            dims=dimensions_for(percent)\n"
"            logical_w=dims[0]\n"
"            logical_h=dims[1]\n"
"            ratio=logical_w.to_f/@base_w.to_f\n"
"            factor=@base_factor/ratio\n"
"            mul=factor*100.0\n"
"            offset_x=@base_offset_x*ratio\n"
"            offset_y=@base_offset_y*ratio\n"
"            set_scale(logical_w,logical_h,factor,mul,offset_x,offset_y)\n"
"            recenter\n"
"            ok=(!recreate_sprites || recreate(scene))\n"
"            if !ok\n"
"              @changing=false\n"
"              restore(scene,recreate_sprites)\n"
"              telemetry(6)\n"
"              return false\n"
"            end\n"
"            @applied_pct=percent\n"
"            @error=0\n"
"            telemetry\n"
"            return true\n"
"          rescue Exception\n"
"            @changing=false\n"
"            restore(scene,recreate_sprites)\n"
"            telemetry(4)\n"
"            return false\n"
"          ensure\n"
"            @changing=false\n"
"          end\n"
"        end\n"
"\n"
"        def request(percent)\n"
"          # Legacy entry point kept harmless for an already loaded v3 DLL.\n"
"          # Only the native atomic value is authoritative now.\n"
"          @target_pct=requested_percent\n"
"        end\n"
"\n"
"        def pump(scene)\n"
"          return if @changing || @suspend_depth.to_i>0\n"
"          return if !stable_map?(scene,true)\n"
"          @target_pct=requested_percent\n"
"          if @target_pct.to_i>100\n"
"            if @applied_pct.to_i!=@target_pct.to_i\n"
"              apply(scene,@target_pct,true)\n"
"            end\n"
"          elsif @applied_pct.to_i!=100\n"
"            restore(scene,true)\n"
"          end\n"
"        end\n"
"\n"
"        def map_enter(scene)\n"
"          telemetry\n"
"          @target_pct=requested_percent\n"
"          return if @suspend_depth.to_i>0 || @target_pct.to_i<=100\n"
"          apply(scene,@target_pct,false) if stable_map?(scene,false)\n"
"        end\n"
"\n"
"        def map_leave(scene)\n"
"          restore(nil,false) if @applied_pct.to_i!=100\n"
"        end\n"
"\n"
"        def with_suspended\n"
"          scene=$scene\n"
"          outer=(@suspend_depth.to_i==0)\n"
"          @suspend_depth=@suspend_depth.to_i+1\n"
"          restore(scene,stable_map?(scene,true)) if outer && @applied_pct.to_i!=100\n"
"          begin\n"
"            return yield\n"
"          ensure\n"
"            @suspend_depth=[@suspend_depth.to_i-1,0].max\n"
"            telemetry if outer && @suspend_depth==0\n"
"            @target_pct=requested_percent if outer && @suspend_depth==0\n"
"            if outer && @suspend_depth==0 && @target_pct.to_i>100\n"
"              current=$scene\n"
"              apply(current,@target_pct,true) if stable_map?(current,true)\n"
"            end\n"
"          end\n"
"        end\n"
"      end\n"
"    end\n"
"\n"
"    if defined?(::Game_Map)\n"
"      class ::Game_Map\n"
"        if method_defined?(:in_range?) &&\n"
"           !method_defined?(:__uranium_camera_in_range_v8)\n"
"          alias __uranium_camera_in_range_v8 in_range?\n"
"          def in_range?(object)\n"
"            if ::UraniumCamera.active?\n"
"              return ::UraniumCamera.event_in_update_range?(self,object)\n"
"            end\n"
"            return __uranium_camera_in_range_v8(object)\n"
"          end\n"
"        end\n"
"      end\n"
"    end\n"
"\n"
"    if defined?(::CustomTilemap)\n"
"      class ::CustomTilemap\n"
"        if method_defined?(:addTile) &&\n"
"           !method_defined?(:__uranium_camera_add_tile_v3)\n"
"          alias __uranium_camera_add_tile_v3 addTile\n"
"          def addTile(tiles,count,xpos,ypos,id)\n"
"            # Le fallback des autotiles animes calcule ses coordonnees dans\n"
"            # le bitmap de cache (@oxLayer0). Un Sprite vit toutefois dans\n"
"            # le viewport : reconvertir dans le repere camera avant rendu.\n"
"            if tiles.equal?(@autosprites)\n"
"              xpos-=(@ox-@oxLayer0)\n"
"              ypos-=(@oy-@oyLayer0)\n"
"            end\n"
"            result=__uranium_camera_add_tile_v3(tiles,count,xpos,ypos,id)\n"
"            begin\n"
"              ::UraniumCamera.fit_tile_sprite(tiles[count],xpos,ypos,\n"
"                 @tileWidth,@tileHeight)\n"
"            rescue Exception\n"
"            end\n"
"            return result\n"
"          end\n"
"        end\n"
"\n"
"        # CustomTilemap utilise normalement bitmap.width/4 comme marge.\n"
"        # A 500 %, cette origine + la largeur du viewport depasse le bitmap,\n"
"        # desactive le cache et force un clear/redessin complet a chaque\n"
"        # frame de scroll. Une marge symetrique garde le src_rect dedans et\n"
"        # laisse 160 px de cache sur chaque bord sans allocation supplementaire.\n"
"        if method_defined?(:refreshLayer0) &&\n"
"           !method_defined?(:__uranium_camera_refresh_layer0_v8)\n"
"          alias __uranium_camera_refresh_layer0_v8 refreshLayer0\n"
"          def refreshLayer0(autotiles=false)\n"
"            return __uranium_camera_refresh_layer0_v8(true) if autotiles\n"
"            return false if @usedsprites\n"
"            bitmap=@layer0.bitmap\n"
"            width=bitmap.width\n"
"            height=bitmap.height\n"
"            view_w=@viewport.rect.width\n"
"            view_h=@viewport.rect.height\n"
"            pad_x=[(width-view_w)/2,0].max\n"
"            pad_y=[(height-view_h)/2,0].max\n"
"            pt_x=@ox-@oxLayer0\n"
"            pt_y=@oy-@oyLayer0\n"
"            if !@firsttime && pt_x>=0 && pt_x+view_w<=width &&\n"
"               pt_y>=0 && pt_y+view_h<=height\n"
"              if @layer0clip && @viewport.ox==0 && @viewport.oy==0\n"
"                @layer0.ox=0\n"
"                @layer0.oy=0\n"
"                @layer0.src_rect.set(pt_x.round,pt_y.round,view_w,view_h)\n"
"              else\n"
"                @layer0.ox=pt_x.round\n"
"                @layer0.oy=pt_y.round\n"
"                @layer0.src_rect.set(0,0,width,height)\n"
"              end\n"
"              return true\n"
"            end\n"
"            @firsttime=false\n"
"            @oxLayer0=(@ox-pad_x).floor\n"
"            @oyLayer0=(@oy-pad_y).floor\n"
"            if @layer0clip\n"
"              @layer0.ox=0\n"
"              @layer0.oy=0\n"
"              @layer0.src_rect.set(pad_x,pad_y,view_w,view_h)\n"
"            else\n"
"              @layer0.ox=pad_x\n"
"              @layer0.oy=pad_y\n"
"            end\n"
"            bitmap.clear\n"
"            map_data=@map_data\n"
"            priorities=@priorities\n"
"            framecount=@framecount\n"
"            tileset=@tileset\n"
"            xsize=map_data.xsize\n"
"            ysize=map_data.ysize\n"
"            zsize=map_data.zsize\n"
"            twidth=@tileWidth\n"
"            theight=@tileHeight\n"
"            x_start=[@oxLayer0/twidth,0].max\n"
"            y_start=[@oyLayer0/theight,0].max\n"
"            x_end=[x_start+(width/twidth)+1,xsize].min\n"
"            y_end=[y_start+(height/theight)+1,ysize].min\n"
"            if x_start<x_end && y_start<y_end\n"
"              tmprect=Rect.new(0,0,0,0)\n"
"              for z in 0...zsize\n"
"                for y in y_start...y_end\n"
"                  ypos=(y*theight)-@oyLayer0\n"
"                  for x in x_start...x_end\n"
"                    xpos=(x*twidth)-@oxLayer0\n"
"                    id=map_data[x,y,z]\n"
"                    next if id==0\n"
"                    priority=priorities[id]\n"
"                    next if !priority || priority!=0\n"
"                    if id>=384\n"
"                      tmprect.set(((id-384)&7)*@tileSrcWidth,\n"
"                         ((id-384)>>3)*@tileSrcHeight,@tileSrcWidth,@tileSrcHeight)\n"
"                      if @diffsizes\n"
"                        bitmap.stretch_blt(Rect.new(xpos,ypos,twidth,theight),\n"
"                           tileset,tmprect)\n"
"                      else\n"
"                        bitmap.blt(xpos,ypos,tileset,tmprect)\n"
"                      end\n"
"                    else\n"
"                      frames=framecount[id/48-1]\n"
"                      frame=(frames<=1) ? 0 :\n"
"                         (Graphics.frame_count/Animated_Autotiles_Frames)%frames\n"
"                      bltAutotile(bitmap,xpos,ypos,id,frame)\n"
"                    end\n"
"                  end\n"
"                end\n"
"              end\n"
"              Graphics.frame_reset\n"
"            end\n"
"            return true\n"
"          rescue Exception\n"
"            return __uranium_camera_refresh_layer0_v8(false)\n"
"          end\n"
"        end\n"
"\n"
"        # Un scroll vanilla rappelle refresh pour chaque variation de ox/oy\n"
"        # et reconfigure alors tous les sprites de priorite visibles. A grand\n"
"        # dezoom, cet ensemble couvre presque toute la carte. Si le rectangle\n"
"        # de tuiles reste identique, decaler les sprites existants est exact et\n"
"        # evite des milliers de addTile/setters par frame.\n"
"        if method_defined?(:refresh) &&\n"
"           !method_defined?(:__uranium_camera_refresh_v8)\n"
"          alias __uranium_camera_refresh_v8 refresh\n"
"          def refresh(autotiles=false)\n"
"            return __uranium_camera_refresh_v8(true) if autotiles\n"
"            view_ox=@viewport.ox\n"
"            view_oy=@viewport.oy\n"
"            view_w=@viewport.rect.width\n"
"            view_h=@viewport.rect.height\n"
"            can_shift=!@firsttime && !@usedsprites && !@tilesetChanged &&\n"
"               !@autotiles.changed && !@nowshown &&\n"
"               @layer0 && @layer0.visible==@visible &&\n"
"               @__uranium_fast_vpox==view_ox &&\n"
"               @__uranium_fast_vpoy==view_oy &&\n"
"               @__uranium_fast_vpw==view_w &&\n"
"               @__uranium_fast_vph==view_h\n"
"            dx=@oldOx-@ox\n"
"            dy=@oldOy-@oy\n"
"            can_shift=false if dx==0 && dy==0\n"
"            xsize=@map_data.xsize\n"
"            ysize=@map_data.ysize\n"
"            min_x=(@ox/@tileWidth)-1\n"
"            max_x=((@ox+view_w)/@tileWidth)+1\n"
"            min_y=(@oy/@tileHeight)-1\n"
"            max_y=((@oy+view_h)/@tileHeight)+1\n"
"            min_x=0 if min_x<0\n"
"            min_x=xsize-1 if min_x>=xsize\n"
"            max_x=0 if max_x<0\n"
"            max_x=xsize-1 if max_x>=xsize\n"
"            min_y=0 if min_y<0\n"
"            min_y=ysize-1 if min_y>=ysize\n"
"            max_y=0 if max_y<0\n"
"            max_y=ysize-1 if max_y>=ysize\n"
"            can_shift=false if !@priotilesrect ||\n"
"               @priotilesrect[0]!=min_x || @priotilesrect[1]!=min_y ||\n"
"               @priotilesrect[2]!=max_x || @priotilesrect[3]!=max_y\n"
"            if can_shift && refreshLayer0(false)\n"
"              refreshFlashSprite\n"
"              i=0\n"
"              while i<@tiles.length\n"
"                sprite=@tiles[i]\n"
"                if sprite.is_a?(Sprite) && !sprite.disposed? && sprite.visible\n"
"                  sprite.x=sprite.x+dx\n"
"                  sprite.y=sprite.y+dy\n"
"                  sprite.z=sprite.z+dy\n"
"                end\n"
"                i+=2\n"
"              end\n"
"              i=0\n"
"              while i<@autosprites.length\n"
"                sprite=@autosprites[i]\n"
"                if sprite.is_a?(Sprite) && !sprite.disposed? && sprite.visible\n"
"                  sprite.x=sprite.x+dx\n"
"                  sprite.y=sprite.y+dy\n"
"                end\n"
"                i+=2\n"
"              end\n"
"              @oldOx=@ox\n"
"              @oldOy=@oy\n"
"              return\n"
"            end\n"
"            result=__uranium_camera_refresh_v8(false)\n"
"            @__uranium_fast_vpox=view_ox\n"
"            @__uranium_fast_vpoy=view_oy\n"
"            @__uranium_fast_vpw=view_w\n"
"            @__uranium_fast_vph=view_h\n"
"            return result\n"
"          rescue Exception\n"
"            return __uranium_camera_refresh_v8(false)\n"
"          end\n"
"        end\n"
"      end\n"
"    end\n"
"\n"
"    # Upgrade a chaud depuis l'essai v7 : retablit le suivi normal.\n"
"    if defined?(::Spriteset_Map)\n"
"      class ::Spriteset_Map\n"
"        if method_defined?(:__uranium_camera_update_v7)\n"
"          alias update __uranium_camera_update_v7\n"
"          begin; remove_method :__uranium_camera_update_v7; rescue Exception; end\n"
"        end\n"
"      end\n"
"    end\n"
"\n"
"    class ::Scene_Map\n"
"      if !method_defined?(:__uranium_camera_main_v3)\n"
"        alias __uranium_camera_main_v3 main\n"
"        def main(*args)\n"
"          ::UraniumCamera.map_enter(self)\n"
"          begin\n"
"            __uranium_camera_main_v3(*args)\n"
"          ensure\n"
"            ::UraniumCamera.map_leave(self)\n"
"          end\n"
"        end\n"
"      end\n"
"\n"
"      if !method_defined?(:__uranium_camera_update_v3)\n"
"        alias __uranium_camera_update_v3 update\n"
"        def update(*args)\n"
"          ::UraniumCamera.pump(self)\n"
"          __uranium_camera_update_v3(*args)\n"
"        end\n"
"      end\n"
"\n"
"      if method_defined?(:call_menu) &&\n"
"         !method_defined?(:__uranium_camera_call_menu_v3)\n"
"        alias __uranium_camera_call_menu_v3 call_menu\n"
"        def call_menu(*args,&block)\n"
"          ::UraniumCamera.with_suspended { __uranium_camera_call_menu_v3(*args,&block) }\n"
"        end\n"
"      end\n"
"    end\n"
"\n"
"    class ::Object\n"
"      if (method_defined?(:pbSceneStandby) || private_method_defined?(:pbSceneStandby)) &&\n"
"         !method_defined?(:__uranium_camera_scene_standby_v3) &&\n"
"         !private_method_defined?(:__uranium_camera_scene_standby_v3)\n"
"        alias __uranium_camera_scene_standby_v3 pbSceneStandby\n"
"        def pbSceneStandby(*args,&block)\n"
"          ::UraniumCamera.with_suspended { __uranium_camera_scene_standby_v3(*args,&block) }\n"
"        end\n"
"        private :pbSceneStandby\n"
"      end\n"
"      if (method_defined?(:pbBattleAnimation) || private_method_defined?(:pbBattleAnimation)) &&\n"
"         !method_defined?(:__uranium_camera_battle_animation_v3) &&\n"
"         !private_method_defined?(:__uranium_camera_battle_animation_v3)\n"
"        alias __uranium_camera_battle_animation_v3 pbBattleAnimation\n"
"        def pbBattleAnimation(*args,&block)\n"
"          ::UraniumCamera.with_suspended { __uranium_camera_battle_animation_v3(*args,&block) }\n"
"        end\n"
"        private :pbBattleAnimation\n"
"      end\n"
"    end\n"
"\n"
"    $__uranium_camera_patch_v8_fixed500=true\n"
"    if ::UraniumCamera.initialized?\n"
"      ::UraniumCamera.update_endpoint($__uranium_camera_native_address,\n"
"         $__uranium_camera_requested_address,$__uranium_camera_client_w,\n"
"         $__uranium_camera_client_h)\n"
"    else\n"
"      ::UraniumCamera.install($__uranium_camera_native_address,\n"
"         $__uranium_camera_requested_address,$__uranium_camera_client_w,\n"
"         $__uranium_camera_client_h)\n"
"    end\n"
"  else\n"
"    if ::UraniumCamera.initialized?\n"
"      ::UraniumCamera.update_endpoint($__uranium_camera_native_address,\n"
"         $__uranium_camera_requested_address,$__uranium_camera_client_w,\n"
"         $__uranium_camera_client_h)\n"
"    else\n"
"      ::UraniumCamera.install($__uranium_camera_native_address,\n"
"         $__uranium_camera_requested_address,$__uranium_camera_client_w,\n"
"         $__uranium_camera_client_h)\n"
"    end\n"
"  end\n"
"rescue Exception\n"
"  begin\n"
"    values=[0,100,0,0,0,0,$__uranium_camera_client_w,\n"
"       $__uranium_camera_client_h,9]\n"
"    $__uranium_camera_copy.call($__uranium_camera_native_address,\n"
"       values.pack(\"l*\"),36)\n"
"  rescue Exception\n"
"  end\n"
"end\n";

static void refresh_client_telemetry() {
    if (!s_game_hwnd) return;
    RECT rc = {0, 0, 0, 0};
    if (!GetClientRect(s_game_hwnd, &rc)) return;
    InterlockedExchange(&g_zoom_telemetry.client_width, rc.right - rc.left);
    InterlockedExchange(&g_zoom_telemetry.client_height, rc.bottom - rc.top);
}

static void capture_base_client_if_needed() {
    if (s_base_client_w > 0 && s_base_client_h > 0) return;
    if (!s_game_hwnd) return;
    RECT rc = {0, 0, 0, 0};
    if (!GetClientRect(s_game_hwnd, &rc)) return;
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    // Uranium applique la taille sauvegardee quelques instants apres la
    // creation de la fenetre. Ne jamais figer la toute premiere taille : le
    // client doit etre stable avant de devenir notre reference immuable.
    const DWORD now = GetTickCount();
    if (width != s_client_candidate_w || height != s_client_candidate_h) {
        s_client_candidate_w = width;
        s_client_candidate_h = height;
        s_client_candidate_since = now;
        return;
    }
    if (s_client_candidate_since == 0 || now - s_client_candidate_since < 1200) {
        return;
    }

    s_base_client_w = width;
    s_base_client_h = height;
    InterlockedExchange(&g_zoom_telemetry.client_width, width);
    InterlockedExchange(&g_zoom_telemetry.client_height, height);
}

static bool install_ruby_patch() {
    if (s_base_client_w <= 0 || s_base_client_h <= 0) return false;

    char prefix[384];
    sprintf_s(prefix, sizeof(prefix),
        "$__uranium_camera_native_address=%lu\n"
        "$__uranium_camera_requested_address=%lu\n"
        "$__uranium_camera_client_w=%d\n"
        "$__uranium_camera_client_h=%d\n",
        (unsigned long)(ULONG_PTR)&g_zoom_telemetry,
        (unsigned long)(ULONG_PTR)&s_requested_percent,
        s_base_client_w, s_base_client_h);

    std::string script(prefix);
    script += s_patch_body;
    const int result = rgss_safe_eval(script.c_str());
    if (result != 0) {
        InterlockedExchange(&g_zoom_telemetry.error,
                            ZOOM_ERR_NATIVE_EVAL);
        return false;
    }
    refresh_client_telemetry();
    return InterlockedCompareExchange(&g_zoom_telemetry.installed, 0, 0) == 1;
}
static void __cdecl on_game_thread_tick(void*) {
    const DWORD now = GetTickCount();
    if (now - s_last_tick < 100) {
        return;
    }
    s_last_tick = now;

    capture_base_client_if_needed();
    refresh_client_telemetry();
    if (InterlockedCompareExchange(&g_zoom_telemetry.installed, 0, 0) == 1)
        return;
    if (s_base_client_w <= 0 || s_base_client_h <= 0) return;
    if (s_last_install_attempt != 0 &&
        now - s_last_install_attempt < 750) return;
    s_last_install_attempt = now;
    install_ruby_patch();
}

void opt_zoom_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    const int saved_zoom = GetPrivateProfileIntA(
        "Settings", "CameraZoom", 100, s_ini);
    g_zoom_value = saved_zoom;
    if (g_zoom_value < OPT_ZOOM_MIN_PERCENT)
        g_zoom_value = OPT_ZOOM_MIN_PERCENT;
    if (g_zoom_value > OPT_ZOOM_MAX_PERCENT)
        g_zoom_value = OPT_ZOOM_MAX_PERCENT;
    if (g_zoom_value != saved_zoom) {
        char normalized[16];
        sprintf_s(normalized, sizeof(normalized), "%d", g_zoom_value);
        WritePrivateProfileStringA(
            "Settings", "CameraZoom", normalized, s_ini);
    }
    InterlockedExchange(&s_requested_percent, g_zoom_value);
}

void opt_zoom_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_base_client_w = 0;
    s_base_client_h = 0;
    s_client_candidate_since = 0;
    s_client_candidate_w = 0;
    s_client_candidate_h = 0;
    InterlockedExchange(&g_zoom_telemetry.client_width, 0);
    InterlockedExchange(&g_zoom_telemetry.client_height, 0);
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    rgss_safe_dispatch_notify();
}

void opt_zoom_apply(int percent) {
    if (percent < OPT_ZOOM_MIN_PERCENT)
        percent = OPT_ZOOM_MIN_PERCENT;
    if (percent > OPT_ZOOM_MAX_PERCENT)
        percent = OPT_ZOOM_MAX_PERCENT;
    g_zoom_value = percent;
    InterlockedExchange(&s_requested_percent, percent);

    char value[16];
    sprintf_s(value, sizeof(value), "%d", percent);
    WritePrivateProfileStringA("Settings", "CameraZoom", value, s_ini);

}

void opt_zoom_get_telemetry(OptZoomTelemetry* out) {
    if (!out) return;
    out->installed = InterlockedCompareExchange(&g_zoom_telemetry.installed, 0, 0);
    out->applied_percent = InterlockedCompareExchange(
        &g_zoom_telemetry.applied_percent, 0, 0);
    out->base_width = InterlockedCompareExchange(&g_zoom_telemetry.base_width, 0, 0);
    out->base_height = InterlockedCompareExchange(&g_zoom_telemetry.base_height, 0, 0);
    out->logical_width = InterlockedCompareExchange(
        &g_zoom_telemetry.logical_width, 0, 0);
    out->logical_height = InterlockedCompareExchange(
        &g_zoom_telemetry.logical_height, 0, 0);
    out->client_width = InterlockedCompareExchange(
        &g_zoom_telemetry.client_width, 0, 0);
    out->client_height = InterlockedCompareExchange(
        &g_zoom_telemetry.client_height, 0, 0);
    out->error = InterlockedCompareExchange(&g_zoom_telemetry.error, 0, 0);
}
