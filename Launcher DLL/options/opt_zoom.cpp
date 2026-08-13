// opt_zoom.cpp - Dezoom de carte RGSS sans agrandir la fenetre.
//
// Le jeu possede deja toute la plomberie necessaire pour changer d'echelle :
// Graphics.width/height sont les dimensions logiques et pbSetResizeFactor2
// adapte les Sprite/Viewport/Plane ainsi que la surface RGSS.  Le trainer
// agrandit donc uniquement les dimensions logiques et applique le facteur
// inverse.  Aucun hook ni aucune capture GDI ne sont necessaires.

#include "../options/opt_zoom.h"
#include "../trainer_runtime.h"

#include <stdio.h>
#include <string>

int g_zoom_value = 100;

volatile OptZoomTelemetry g_zoom_telemetry = {0};

static char          s_ini[MAX_PATH] = {0};
static HWND          s_game_hwnd = NULL;
static DWORD         s_game_tid = 0;
static int           s_base_client_w = 0;
static int           s_base_client_h = 0;
static volatile LONG s_pending = 1;
static volatile LONG s_in_tick = 0;
static DWORD         s_last_tick = 0;
static DWORD         s_last_install_attempt = 0;
static DWORD         s_force_client_until = 0;
static DWORD         s_client_candidate_since = 0;
static int           s_client_candidate_w = 0;
static int           s_client_candidate_h = 0;

static HHOOK s_hook_cwp = NULL;
static HHOOK s_hook_getmsg = NULL;

typedef int (__cdecl *RGSSEval_t)(const char*);
static RGSSEval_t s_eval = NULL;

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
// All mutations happen on the RGSS thread through the window hooks below.
static const char s_patch_body[] =
"begin\n"
"  $__uranium_camera_copy = Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\")\n"
"  ::Graphics.module_eval(\"def self.dll_camera_set_size(w,h)\\n\"+\n"
"     \"  @@width=w.to_i\\n  @@height=h.to_i\\nend\\n\")\n"
"  if !(defined?($__uranium_camera_patch_v3) && $__uranium_camera_patch_v3)\n"
"    module ::UraniumCamera\n"
"      class << self\n"
"        def install(native_address,client_w,client_h)\n"
"          @native_address=native_address.to_i\n"
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
"        def update_endpoint(native_address,client_w,client_h)\n"
"          @native_address=native_address.to_i\n"
"          @client_w=client_w.to_i if client_w.to_i>0\n"
"          @client_h=client_h.to_i if client_h.to_i>0\n"
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
"        def stable_map?(scene,need_spritesets=true)\n"
"          return false if !scene || !scene.is_a?(Scene_Map)\n"
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
"          return true if !scene || !scene.is_a?(Scene_Map)\n"
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
"            @applied_pct=100\n"
"            @logical_w=@base_w\n"
"            @logical_h=@base_h\n"
"            telemetry(5)\n"
"            return false\n"
"          ensure\n"
"            @changing=false\n"
"          end\n"
"        end\n"
"\n"
"        def apply(scene,percent,recreate_sprites=true)\n"
"          percent=percent.to_i\n"
"          percent=100 if percent<100\n"
"          percent=300 if percent>300\n"
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
"          percent=percent.to_i\n"
"          percent=100 if percent<100\n"
"          percent=300 if percent>300\n"
"          @target_pct=percent\n"
"          scene=$scene\n"
"          if percent==100\n"
"            restore(scene,stable_map?(scene,true))\n"
"          elsif @suspend_depth.to_i==0 && stable_map?(scene,true)\n"
"            apply(scene,percent,true)\n"
"          else\n"
"            telemetry(3)\n"
"          end\n"
"        end\n"
"\n"
"        def pump(scene)\n"
"          return if @changing || @suspend_depth.to_i>0\n"
"          return if !stable_map?(scene,true)\n"
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
"            if outer && @suspend_depth==0 && @target_pct.to_i>100\n"
"              current=$scene\n"
"              apply(current,@target_pct,true) if stable_map?(current,true)\n"
"            end\n"
"          end\n"
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
"    $__uranium_camera_patch_v3=true\n"
"    ::UraniumCamera.install($__uranium_camera_native_address,\n"
"       $__uranium_camera_client_w,$__uranium_camera_client_h)\n"
"  else\n"
"    if ::UraniumCamera.initialized?\n"
"      ::UraniumCamera.update_endpoint($__uranium_camera_native_address,\n"
"         $__uranium_camera_client_w,$__uranium_camera_client_h)\n"
"    else\n"
"      ::UraniumCamera.install($__uranium_camera_native_address,\n"
"         $__uranium_camera_client_w,$__uranium_camera_client_h)\n"
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

static bool resolve_eval() {
    if (s_eval) return true;
    HMODULE rgss = GetModuleHandleA("RGSS102E.dll");
    if (!rgss) return false;
    s_eval = (RGSSEval_t)GetProcAddress(rgss, "RGSSEval");
    return s_eval != NULL;
}

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

// SetWindowPos is corrected using the measured client delta rather than a
// guessed border size.  This also behaves correctly with DPI-aware borders.
static bool force_base_client_size() {
    if (!s_game_hwnd || s_base_client_w <= 0 || s_base_client_h <= 0) return false;

    for (int pass = 0; pass < 2; ++pass) {
        RECT client = {0, 0, 0, 0};
        RECT window = {0, 0, 0, 0};
        if (!GetClientRect(s_game_hwnd, &client) ||
            !GetWindowRect(s_game_hwnd, &window)) return false;

        const int current_w = client.right - client.left;
        const int current_h = client.bottom - client.top;
        if (current_w == s_base_client_w && current_h == s_base_client_h) {
            refresh_client_telemetry();
            return true;
        }

        const int outer_w = window.right - window.left;
        const int outer_h = window.bottom - window.top;
        if (!SetWindowPos(s_game_hwnd, NULL, 0, 0,
                outer_w + (s_base_client_w - current_w),
                outer_h + (s_base_client_h - current_h),
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) return false;
    }

    refresh_client_telemetry();
    return g_zoom_telemetry.client_width == s_base_client_w &&
           g_zoom_telemetry.client_height == s_base_client_h;
}

static bool install_ruby_patch() {
    if (!s_eval || s_base_client_w <= 0 || s_base_client_h <= 0) return false;

    char prefix[384];
    sprintf_s(prefix, sizeof(prefix),
        "$__uranium_camera_native_address=%lu\n"
        "$__uranium_camera_client_w=%d\n"
        "$__uranium_camera_client_h=%d\n",
        (unsigned long)(ULONG_PTR)&g_zoom_telemetry,
        s_base_client_w, s_base_client_h);

    std::string script(prefix);
    script += s_patch_body;
    s_eval(script.c_str());
    refresh_client_telemetry();
    return InterlockedCompareExchange(&g_zoom_telemetry.installed, 0, 0) == 1;
}

static void send_zoom_request(int percent) {
    if (!s_eval) return;
    char ruby[256];
    sprintf_s(ruby, sizeof(ruby),
        "begin; ::UraniumCamera.request(%d) if defined?(::UraniumCamera); "
        "rescue Exception; end", percent);
    s_eval(ruby);
    refresh_client_telemetry();
}

static void update_ruby_client_size() {
    if (!s_eval || s_base_client_w <= 0 || s_base_client_h <= 0) return;
    char ruby[256];
    sprintf_s(ruby, sizeof(ruby),
        "begin; ::UraniumCamera.update_client(%d,%d) "
        "if defined?(::UraniumCamera); rescue Exception; end",
        s_base_client_w, s_base_client_h);
    s_eval(ruby);
}

static void post_to_game() {
    if (s_game_hwnd) PostMessageA(s_game_hwnd, WM_NULL, 0, 0);
    if (s_game_tid) PostThreadMessageA(s_game_tid, WM_NULL, 0, 0);
}

static void on_game_thread_tick() {
    if (InterlockedCompareExchange(&s_in_tick, 1, 0) != 0) return;

    const DWORD now = GetTickCount();
    const bool urgent = InterlockedCompareExchange(&s_pending, 0, 0) != 0;
    if (!urgent && now - s_last_tick < 100) {
        InterlockedExchange(&s_in_tick, 0);
        return;
    }
    s_last_tick = now;

    if (!resolve_eval()) {
        InterlockedExchange(&s_in_tick, 0);
        return;
    }

    capture_base_client_if_needed();
    refresh_client_telemetry();
    const bool installed =
        InterlockedCompareExchange(&g_zoom_telemetry.installed, 0, 0) == 1;
    if (!installed && (s_last_install_attempt == 0 ||
                       now - s_last_install_attempt >= 750)) {
        s_last_install_attempt = now;
        if (!install_ruby_patch()) {
            InterlockedExchange(&g_zoom_telemetry.error, ZOOM_ERR_NATIVE_EVAL);
        }
    }

    if (InterlockedCompareExchange(&g_zoom_telemetry.installed, 0, 0) == 1) {
        if (InterlockedExchange(&s_pending, 0) != 0) {
            update_ruby_client_size();
            int percent = g_zoom_value;
            if (percent < 100) percent = 100;
            if (percent > 300) percent = 300;
            send_zoom_request(percent);
            s_force_client_until = now + 1500;
        }

        const LONG applied =
            InterlockedCompareExchange(&g_zoom_telemetry.applied_percent, 0, 0);
        if (applied > 100 || (LONG)(s_force_client_until - now) > 0) {
            if (!force_base_client_size()) {
                InterlockedExchange(&g_zoom_telemetry.error, ZOOM_ERR_CLIENT_SIZE);
            }
        }
    }

    InterlockedExchange(&s_in_tick, 0);
}

static LRESULT CALLBACK cwp_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_cwp, code, wp, lp);
}

static LRESULT CALLBACK getmsg_hook(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) on_game_thread_tick();
    return CallNextHookEx(s_hook_getmsg, code, wp, lp);
}

static void install_hooks() {
    if (!s_game_tid || !g_trainer_module) return;
    if (!s_hook_cwp) {
        s_hook_cwp = SetWindowsHookExA(
            WH_CALLWNDPROC, cwp_hook, g_trainer_module, s_game_tid);
    }
    if (!s_hook_getmsg) {
        s_hook_getmsg = SetWindowsHookExA(
            WH_GETMESSAGE, getmsg_hook, g_trainer_module, s_game_tid);
    }
}

void opt_zoom_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
    g_zoom_value = GetPrivateProfileIntA("Settings", "CameraZoom", 100, s_ini);
    if (g_zoom_value < 100) g_zoom_value = 100;
    if (g_zoom_value > 300) g_zoom_value = 300;
    resolve_eval();
}

void opt_zoom_set_hwnd_and_start(HWND hwnd) {
    s_game_hwnd = hwnd;
    s_game_tid = hwnd ? GetWindowThreadProcessId(hwnd, NULL) : 0;
    s_base_client_w = 0;
    s_base_client_h = 0;
    s_client_candidate_since = 0;
    s_client_candidate_w = 0;
    s_client_candidate_h = 0;
    InterlockedExchange(&g_zoom_telemetry.client_width, 0);
    InterlockedExchange(&g_zoom_telemetry.client_height, 0);
    InterlockedExchange(&s_pending, 1);
    install_hooks();
    post_to_game();
}

void opt_zoom_apply(int percent) {
    if (percent < 100) percent = 100;
    if (percent > 300) percent = 300;
    g_zoom_value = percent;

    char value[16];
    sprintf_s(value, sizeof(value), "%d", percent);
    WritePrivateProfileStringA("Settings", "CameraZoom", value, s_ini);

    InterlockedExchange(&s_pending, 1);
    post_to_game();
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
