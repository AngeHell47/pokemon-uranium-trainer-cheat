#include "opt_autosave.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>
#include <string.h>

bool g_autosave_enabled = true;

namespace {

static const DWORD kAutosaveIntervalMs = 60000;
static const DWORD kUnsafeRetryMs = 1000;
static const int kSlotCount = 10;

static char s_ini_path[MAX_PATH] = {};
static volatile LONG s_bootstrapped = 0;
static volatile LONG s_refresh_pending = 0;
static volatile LONG s_load_pending = 0;
static volatile LONG s_action_result = 0;
static volatile LONG s_status = OPT_AUTOSAVE_DISABLED;
static volatile LONG s_revision = 0;
static volatile LONG s_slot_valid[kSlotCount] = {};
static volatile LONG s_slot_timestamps[kSlotCount] = {};
static volatile LONG s_slot_sizes[kSlotCount] = {};
static DWORD s_next_save_tick = 0;
static DWORD s_retry_tick = 0;
static DWORD s_feedback_until = 0;
static char s_bootstrap_ruby[16384] = {};
static char s_refresh_ruby[2048] = {};
static char s_save_ruby[1024] = {};

static bool tick_reached(DWORD now, DWORD target) {
    return (LONG)(now - target) >= 0;
}

static void write_setting() {
    if (!s_ini_path[0]) return;
    WritePrivateProfileStringA("Autosave", "Enabled",
        g_autosave_enabled ? "1" : "0", s_ini_path);
}

static void build_ruby_scripts() {
    _snprintf_s(s_bootstrap_ruby, sizeof(s_bootstrap_ruby), _TRUNCATE,
        "module UraniumTrainerAutosave\n"
        "  SLOT_COUNT=10\n"
        "  STATUS_ADDRESS=%lu\n"
        "  def self.writer\n"
        "    @writer ||= Win32API.new('kernel32','RtlMoveMemory',['l','p','l'],'v')\n"
        "  end\n"
        "  def self.write_status(value)\n"
        "    writer.call(STATUS_ADDRESS,[value].pack('l'),4)\n"
        "  rescue Exception\n"
        "  end\n"
        "  def self.directory\n"
        "    root=File.dirname(RTP.getSaveFileName('UraniumTrainerProbe.rxdata'))\n"
        "    File.join(root,'UraniumTrainerAutosaves')\n"
        "  end\n"
        "  def self.slot_path(slot)\n"
        "    File.join(directory,sprintf('Autosave_%%02d.rxdata',slot.to_i))\n"
        "  end\n"
        "  def self.safe_moment?\n"
        "    return false if !defined?(Scene_Map) || !$scene || !$scene.is_a?(Scene_Map)\n"
        "    return false if !defined?($Trainer) || !$Trainer || !defined?($game_map) || !$game_map\n"
        "    return false if !defined?($MapFactory) || !$MapFactory || !defined?($game_player) || !$game_player\n"
        "    return false if $game_system && ($game_system.save_disabled rescue false)\n"
        "    return false if $game_temp && (($game_temp.message_window_showing rescue false) || ($game_temp.player_transferring rescue false) || ($game_temp.transition_processing rescue false) || ($game_temp.menu_calling rescue false) || ($game_temp.in_menu rescue false) || ($game_temp.battle_calling rescue false) || ($game_temp.shop_calling rescue false) || ($game_temp.name_calling rescue false) || ($game_temp.save_calling rescue false) || ($game_temp.debug_calling rescue false))\n"
        "    return false if $PokemonTemp && (($PokemonTemp.miniupdate rescue false) || ($PokemonTemp.hiddenMoveEventCalling rescue false) || ($PokemonTemp.keyItemCalling rescue false))\n"
        "    return false if ($game_player.moving? rescue true) || ($game_player.move_route_forcing rescue true)\n"
        "    return false if (pbMapInterpreterRunning? rescue true)\n"
        "    true\n"
        "  rescue Exception\n"
        "    false\n"
        "  end\n"
        "  def self.ensure_directory\n"
        "    Dir.mkdir(directory) if !FileTest.directory?(directory)\n"
        "    FileTest.directory?(directory)\n"
        "  end\n"
        "  def self.save_now\n"
        "    return 0 if !safe_moment?\n"
        "    ensure_directory\n"
        "    slot=nil\n"
        "    oldest_time=nil\n"
        "    1.upto(SLOT_COUNT) do |candidate|\n"
        "      path=slot_path(candidate)\n"
        "      if !FileTest.file?(path)\n"
        "        slot=candidate\n"
        "        break\n"
        "      end\n"
        "      stamp=(File.mtime(path).to_i rescue 0)\n"
        "      if oldest_time.nil? || stamp<oldest_time\n"
        "        oldest_time=stamp\n"
        "        slot=candidate\n"
        "      end\n"
        "    end\n"
        "    path=slot_path(slot)\n"
        "    temporary=path+'.temp'\n"
        "    begin; File.delete(temporary) if FileTest.file?(temporary); rescue Exception; end\n"
        "    total=$totalPlayTime+(Time.now.to_i-$loadTime)\n"
        "    File.open(temporary,'wb') do |file|\n"
        "      Marshal.dump($Trainer,file)\n"
        "      Marshal.dump('T'+total.to_i.to_s,file)\n"
        "      Marshal.dump($game_system,file)\n"
        "      Marshal.dump(PokemonSystem.new,file)\n"
        "      Marshal.dump($game_map.map_id,file)\n"
        "      Marshal.dump($game_switches,file)\n"
        "      Marshal.dump($game_variables,file)\n"
        "      Marshal.dump($game_self_switches,file)\n"
        "      Marshal.dump($game_screen,file)\n"
        "      Marshal.dump($MapFactory,file)\n"
        "      Marshal.dump($game_player,file)\n"
        "      Marshal.dump($PokemonGlobal,file)\n"
        "      Marshal.dump($PokemonMap,file)\n"
        "      Marshal.dump($PokemonBag,file)\n"
        "      Marshal.dump($PokemonStorage,file)\n"
        "    end\n"
        "    raise 'Autosave integrity check failed' if !SaveSystem.integrityCheck(temporary)\n"
        "    mover=Win32API.new('kernel32','MoveFileExA',['p','p','l'],'i')\n"
        "    raise 'Autosave atomic replace failed' if mover.call(temporary,path,9)==0\n"
        "    2\n"
        "  rescue Exception => error\n"
        "    begin; File.delete(temporary) if temporary && FileTest.file?(temporary); rescue Exception; end\n"
        "    -1\n"
        "  end\n"
        "  def self.refresh_infos(valid_address,time_address,size_address)\n"
        "    valid=[]; times=[]; sizes=[]\n"
        "    1.upto(SLOT_COUNT) do |slot|\n"
        "      path=slot_path(slot)\n"
        "      good=FileTest.file?(path)\n"
        "      valid << (good ? 1 : 0)\n"
        "      times << (good ? (File.mtime(path).to_i rescue 0) : 0)\n"
        "      sizes << (good ? (File.size(path).to_i rescue 0) : 0)\n"
        "    end\n"
        "    writer.call(valid_address,valid.pack('l*'),SLOT_COUNT*4)\n"
        "    writer.call(time_address,times.pack('l*'),SLOT_COUNT*4)\n"
        "    writer.call(size_address,sizes.pack('l*'),SLOT_COUNT*4)\n"
        "    true\n"
        "  rescue Exception\n"
        "    false\n"
        "  end\n"
        "  def self.request_load(slot)\n"
        "    return 0 if !safe_moment?\n"
        "    path=slot_path(slot)\n"
        "    return -1 if slot.to_i<1 || slot.to_i>SLOT_COUNT || !FileTest.file?(path) || !SaveSystem.integrityCheck(path)\n"
        "    $scene=UraniumTrainerAutosaveLoader.new(path)\n"
        "    1\n"
        "  rescue Exception\n"
        "    -1\n"
        "  end\n"
        "end\n"
        "class UraniumTrainerAutosaveLoader\n"
        "  def initialize(path); @path=path; end\n"
        "  def main\n"
        "    begin\n"
        "      raise 'Invalid autosave' if !SaveSystem.integrityCheck(@path)\n"
        "      data=[]\n"
        "      File.open(@path){|file| 15.times { data << Marshal.oldload(file) }}\n"
        "      trainer=data[0]; playtime=data[1]; game_system=data[2]\n"
        "      map_factory=data[9]; player=data[10]; global=data[11]; metadata=data[12]\n"
        "      raise 'Invalid map' if !map_factory || !map_factory.map || !player || !global || !metadata\n"
        "      $PokemonTemp=PokemonTemp.new\n"
        "      $game_temp=Game_Temp.new\n"
        "      $Trainer=trainer\n"
        "      $totalPlayTime=playtime.is_a?(Numeric) ? playtime.to_i/50 : playtime[1..-1].to_i\n"
        "      $loadTime=Time.now.to_i\n"
        "      $game_system=game_system\n"
        "      $game_switches=data[5]\n"
        "      $game_variables=data[6]\n"
        "      $game_self_switches=data[7]\n"
        "      $game_screen=data[8]\n"
        "      $MapFactory=map_factory\n"
        "      $game_map=$MapFactory.map\n"
        "      $game_player=player\n"
        "      $PokemonGlobal=global\n"
        "      $PokemonMap=metadata\n"
        "      $PokemonBag=data[13]\n"
        "      $PokemonStorage=data[14]\n"
        "      magic=($data_system.respond_to?('magic_number') ? $data_system.magic_number : $data_system.version_id)\n"
        "      if $game_system.magic_number!=magic || $PokemonGlobal.safesave\n"
        "        pbMapInterpreter.setup(nil,0) if (pbMapInterpreterRunning? rescue false)\n"
        "        $MapFactory.setup($game_map.map_id)\n"
        "      else\n"
        "        $MapFactory.setMapChanged($game_map.map_id)\n"
        "      end\n"
        "      raise 'Corrupt map' if !$game_map || !$game_map.events\n"
        "      if $game_screen.tone==Tone.new(-255,-255,-255,0)\n"
        "        $game_screen.start_tone_change(Tone.new(0,0,0,0),0)\n"
        "      end\n"
        "      $PokemonEncounters=PokemonEncounters.new\n"
        "      $PokemonEncounters.setup($game_map.map_id)\n"
        "      pbAutoplayOnSave\n"
        "      $game_map.update\n"
        "      $PokemonMap.updateMap\n"
        "      Graphics.frame_reset\n"
        "      $scene=Scene_Map.new\n"
        "      UraniumTrainerAutosave.write_status(6)\n"
        "    rescue Exception => error\n"
        "      UraniumTrainerAutosave.write_status(-2)\n"
        "      $scene=Scene_Map.new if !$scene || $scene.is_a?(UraniumTrainerAutosaveLoader)\n"
        "    end\n"
        "  end\n"
        "end\n",
        (unsigned long)(ULONG_PTR)&s_status);

    _snprintf_s(s_refresh_ruby, sizeof(s_refresh_ruby), _TRUNCATE,
        "begin\n"
        " UraniumTrainerAutosave.refresh_infos(%lu,%lu,%lu)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)(ULONG_PTR)&s_slot_valid[0],
        (unsigned long)(ULONG_PTR)&s_slot_timestamps[0],
        (unsigned long)(ULONG_PTR)&s_slot_sizes[0]);

    _snprintf_s(s_save_ruby, sizeof(s_save_ruby), _TRUNCATE,
        "result=-1\n"
        "begin; result=UraniumTrainerAutosave.save_now; rescue Exception; result=-1; end\n"
        "begin\n"
        " writer=Win32API.new('kernel32','RtlMoveMemory',['l','p','l'],'v')\n"
        " writer.call(%lu,[result].pack('l'),4)\n"
        "rescue Exception\n"
        "end\n",
        (unsigned long)(ULONG_PTR)&s_action_result);
}

static bool run_refresh() {
    if (rgss_safe_eval(s_refresh_ruby) != 0) return false;
    InterlockedIncrement(&s_revision);
    return true;
}

static LONG run_save() {
    InterlockedExchange(&s_action_result, -99);
    if (rgss_safe_eval(s_save_ruby) != 0) return -1;
    return InterlockedExchangeAdd(&s_action_result, 0);
}

static LONG run_load(int slot) {
    char ruby[1024] = {};
    _snprintf_s(ruby, sizeof(ruby), _TRUNCATE,
        "result=-1\n"
        "begin; result=UraniumTrainerAutosave.request_load(%d); rescue Exception; result=-1; end\n"
        "begin\n"
        " writer=Win32API.new('kernel32','RtlMoveMemory',['l','p','l'],'v')\n"
        " writer.call(%lu,[result].pack('l'),4)\n"
        "rescue Exception\n"
        "end\n",
        slot, (unsigned long)(ULONG_PTR)&s_action_result);
    InterlockedExchange(&s_action_result, -99);
    if (rgss_safe_eval(ruby) != 0) return -1;
    return InterlockedExchangeAdd(&s_action_result, 0);
}

static void __cdecl on_game_thread_tick(void*) {
    if (!InterlockedExchangeAdd(&s_bootstrapped, 0)) {
        if (rgss_safe_eval(s_bootstrap_ruby) != 0) return;
        InterlockedExchange(&s_bootstrapped, 1);
        InterlockedExchange(&s_refresh_pending, 1);
    }

    if (InterlockedExchange(&s_refresh_pending, 0)) {
        if (!run_refresh()) InterlockedExchange(&s_refresh_pending, 1);
    }

    const LONG load_slot = InterlockedExchange(&s_load_pending, 0);
    if (load_slot > 0) {
        InterlockedExchange(&s_status, OPT_AUTOSAVE_LOADING);
        const LONG result = run_load((int)load_slot);
        if (result == 0)
            InterlockedExchange(&s_status, OPT_AUTOSAVE_LOAD_REFUSED);
        else if (result < 0)
            InterlockedExchange(&s_status, OPT_AUTOSAVE_LOAD_ERROR);
        return;
    }

    const DWORD now = GetTickCount();
    if (!g_autosave_enabled) {
        s_next_save_tick = 0;
        s_retry_tick = 0;
        s_feedback_until = 0;
        InterlockedExchange(&s_status, OPT_AUTOSAVE_DISABLED);
        return;
    }

    // The Ruby loader completes outside the Graphics.update callback. Start a
    // fresh full interval once it reports success, rather than immediately
    // replacing the state that was just restored when the old deadline passed.
    if (InterlockedExchangeAdd(&s_status, 0) == OPT_AUTOSAVE_LOADED &&
        !s_feedback_until) {
        s_next_save_tick = now + kAutosaveIntervalMs;
        s_retry_tick = 0;
        s_feedback_until = now + 2500;
    }

    if (!s_next_save_tick) {
        s_next_save_tick = now + kAutosaveIntervalMs;
        InterlockedExchange(&s_status, OPT_AUTOSAVE_COUNTDOWN);
    }
    if (s_feedback_until && tick_reached(now, s_feedback_until)) {
        s_feedback_until = 0;
        InterlockedExchange(&s_status, OPT_AUTOSAVE_COUNTDOWN);
    }
    if (!tick_reached(now, s_next_save_tick)) return;
    if (s_retry_tick && !tick_reached(now, s_retry_tick)) return;

    InterlockedExchange(&s_status, OPT_AUTOSAVE_SAVING);
    const LONG result = run_save();
    if (result == 2) {
        s_next_save_tick = now + kAutosaveIntervalMs;
        s_retry_tick = 0;
        s_feedback_until = now + 2500;
        InterlockedExchange(&s_status, OPT_AUTOSAVE_SAVED);
        InterlockedExchange(&s_refresh_pending, 1);
    } else if (result == 0) {
        s_retry_tick = now + kUnsafeRetryMs;
        InterlockedExchange(&s_status, OPT_AUTOSAVE_WAITING_SAFE);
    } else {
        s_next_save_tick = now + kAutosaveIntervalMs;
        s_retry_tick = 0;
        InterlockedExchange(&s_status, OPT_AUTOSAVE_ERROR);
    }
}

} // namespace

bool opt_autosave_init(const char* ini_path) {
    lstrcpynA(s_ini_path, ini_path ? ini_path : "", ARRAYSIZE(s_ini_path));
    g_autosave_enabled = GetPrivateProfileIntA(
        "Autosave", "Enabled", 1, s_ini_path) != 0;
    ZeroMemory((void*)s_slot_valid, sizeof(s_slot_valid));
    ZeroMemory((void*)s_slot_timestamps, sizeof(s_slot_timestamps));
    ZeroMemory((void*)s_slot_sizes, sizeof(s_slot_sizes));
    InterlockedExchange(&s_bootstrapped, 0);
    InterlockedExchange(&s_refresh_pending, 0);
    InterlockedExchange(&s_load_pending, 0);
    InterlockedExchange(&s_revision, 0);
    InterlockedExchange(&s_status,
        g_autosave_enabled ? OPT_AUTOSAVE_COUNTDOWN : OPT_AUTOSAVE_DISABLED);
    s_next_save_tick = g_autosave_enabled ? GetTickCount() + kAutosaveIntervalMs : 0;
    s_retry_tick = 0;
    s_feedback_until = 0;
    build_ruby_scripts();
    return rgss_safe_dispatch_register(on_game_thread_tick, NULL);
}

void opt_autosave_shutdown() {
    InterlockedExchange(&s_load_pending, 0);
    InterlockedExchange(&s_refresh_pending, 0);
    rgss_safe_dispatch_unregister(on_game_thread_tick, NULL);
}

void opt_autosave_toggle(bool enabled) {
    g_autosave_enabled = enabled;
    const DWORD now = GetTickCount();
    s_next_save_tick = enabled ? now + kAutosaveIntervalMs : 0;
    s_retry_tick = 0;
    s_feedback_until = 0;
    InterlockedExchange(&s_status,
        enabled ? OPT_AUTOSAVE_COUNTDOWN : OPT_AUTOSAVE_DISABLED);
    write_setting();
    rgss_safe_dispatch_notify();
}

void opt_autosave_request_refresh() {
    InterlockedExchange(&s_refresh_pending, 1);
    rgss_safe_dispatch_notify();
}

void opt_autosave_request_load(int physical_slot) {
    if (physical_slot < 1 || physical_slot > kSlotCount) return;
    InterlockedExchange(&s_load_pending, physical_slot);
    InterlockedExchange(&s_status, OPT_AUTOSAVE_LOADING);
    rgss_safe_dispatch_notify();
}

OptAutosaveSlot opt_autosave_slot(int physical_slot) {
    OptAutosaveSlot result = {};
    if (physical_slot < 1 || physical_slot > kSlotCount) return result;
    const int index = physical_slot - 1;
    result.valid = InterlockedExchangeAdd(&s_slot_valid[index], 0) != 0;
    result.timestamp = InterlockedExchangeAdd(&s_slot_timestamps[index], 0);
    result.size_bytes = InterlockedExchangeAdd(&s_slot_sizes[index], 0);
    return result;
}

LONG opt_autosave_status() {
    return InterlockedExchangeAdd(&s_status, 0);
}

LONG opt_autosave_revision() {
    return InterlockedExchangeAdd(&s_revision, 0);
}

int opt_autosave_seconds_until_next() {
    if (!g_autosave_enabled || !s_next_save_tick) return 0;
    const DWORD now = GetTickCount();
    if (tick_reached(now, s_next_save_tick)) return 0;
    return (int)((s_next_save_tick - now + 999) / 1000);
}
