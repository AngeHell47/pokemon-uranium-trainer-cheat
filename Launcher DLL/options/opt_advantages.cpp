#include "opt_advantages.h"
#include "../rgss_safe_dispatch.h"

#include <stdio.h>

bool g_always_first_enabled       = false;
bool g_perfect_accuracy_enabled   = false;
bool g_guaranteed_critical_enabled = false;
bool g_status_immunity_enabled    = false;
bool g_guaranteed_flee_enabled    = false;
bool g_instant_fishing_enabled    = false;
bool g_guaranteed_fishing_enabled = false;
int  g_exp_multiplier             = 1;
int  g_prize_money_multiplier     = 1;

static char s_ini[MAX_PATH] = {};
static volatile LONG s_always_first = 0;
static volatile LONG s_accuracy = 0;
static volatile LONG s_critical = 0;
static volatile LONG s_status_immunity = 0;
static volatile LONG s_flee = 0;
static volatile LONG s_instant_fishing = 0;
static volatile LONG s_guaranteed_fishing = 0;
static volatile LONG s_exp_multiplier = 1;
static volatile LONG s_money_multiplier = 1;
static volatile LONG s_pending = 0;
static volatile LONG s_installed = 0;
static volatile LONG s_retry_started = 0;
static char s_ruby[32768] = {};

static int clamp_multiplier(int value) {
    if (value < 1) return 1;
    if (value > 100) return 100;
    return value;
}

static void notify_game() { rgss_safe_dispatch_notify(); }

static void build_ruby() {
    _snprintf_s(s_ruby, sizeof(s_ruby), _TRUNCATE,
        "installed=0\n"
        "begin\n"
        "  $__uranium_trainer_always_first=%s\n"
        "  $__uranium_trainer_perfect_accuracy=%s\n"
        "  $__uranium_trainer_guaranteed_critical=%s\n"
        "  $__uranium_trainer_status_immunity=%s\n"
        "  $__uranium_trainer_guaranteed_flee=%s\n"
        "  $__uranium_trainer_instant_fishing=false\n"
        "  $__uranium_trainer_guaranteed_fishing=%s\n"
        "  $__uranium_trainer_exp_multiplier=%d\n"
        "  $__uranium_trainer_money_multiplier=%d\n"
        "  $__uranium_trainer_exp_scope||=0\n"
        "  $__uranium_trainer_flee_scope||=0\n"
        "  $__uranium_trainer_fishing_scope||=0\n"
        "  if defined?(::PokeBattle_Battle) && defined?(::PokeBattle_Move) &&\n"
        "     defined?(::PokeBattle_Battler) && defined?(::PokeBattle_Pokemon) &&\n"
        "     defined?(::PokeBattle_Trainer) && defined?(::PBExperience)\n"
        "    class ::PokeBattle_Battle\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_adv_original_priority\")\n"
        "        alias_method :__uranium_adv_original_priority, :pbPriority\n"
        "        alias_method :__uranium_adv_original_run, :pbRun\n"
        "        alias_method :__uranium_adv_original_ai_random, :pbAIRandom\n"
        "        alias_method :__uranium_adv_original_gain_exp, :pbGainEXP\n"
        "        alias_method :__uranium_adv_original_end_battle, :pbEndOfBattle\n"
        "      end\n"
        "      def pbPriority(ignorequickclaw=false)\n"
        "        order=__uranium_adv_original_priority(ignorequickclaw)\n"
        "        return order if !$__uranium_trainer_always_first\n"
        "        begin\n"
        "          first=[]; rest=[]\n"
        "          order.each do |b|\n"
        "            mine=b && pbOwnedByPlayer?(b.index) && @choices[b.index] && @choices[b.index][0]==1\n"
        "            (mine ? first : rest).push(b)\n"
        "          end\n"
        "          order=first+rest\n"
        "          @priority=order\n"
        "        rescue Exception\n"
        "        end\n"
        "        order\n"
        "      end\n"
        "      def pbRun(idxPokemon,duringBattle=false)\n"
        "        scoped=false\n"
        "        begin\n"
        "          scoped=$__uranium_trainer_guaranteed_flee && !@opponent && !pbIsOpposing?(idxPokemon)\n"
        "          $__uranium_trainer_flee_scope+=1 if scoped\n"
        "          __uranium_adv_original_run(idxPokemon,duringBattle)\n"
        "        ensure\n"
        "          $__uranium_trainer_flee_scope-=1 if scoped\n"
        "        end\n"
        "      end\n"
        "      def pbAIRandom(limit)\n"
        "        return 0 if $__uranium_trainer_flee_scope.to_i>0\n"
        "        __uranium_adv_original_ai_random(limit)\n"
        "      end\n"
        "      def pbGainEXP\n"
        "        $__uranium_trainer_exp_scope+=1\n"
        "        begin\n"
        "          __uranium_adv_original_gain_exp\n"
        "        ensure\n"
        "          $__uranium_trainer_exp_scope-=1\n"
        "        end\n"
        "      end\n"
        "      def pbEndOfBattle(canlose=false)\n"
        "        old_extra=nil\n"
        "        begin\n"
        "          mult=$__uranium_trainer_money_multiplier.to_i\n"
        "          if mult>1 && @decision==1 && @internalbattle && @extramoney.to_i>0\n"
        "            old_extra=@extramoney\n"
        "            @extramoney=@extramoney.to_i*mult\n"
        "          end\n"
        "          __uranium_adv_original_end_battle(canlose)\n"
        "        ensure\n"
        "          @extramoney=old_extra if !old_extra.nil?\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    class << ::PBExperience\n"
        "      unless method_defined?(:__uranium_adv_original_add_experience)\n"
        "        alias_method :__uranium_adv_original_add_experience, :pbAddExperience\n"
        "      end\n"
        "      def pbAddExperience(current,gain,growth)\n"
        "        if $__uranium_trainer_exp_scope.to_i>0\n"
        "          gain=gain.to_i*$__uranium_trainer_exp_multiplier.to_i\n"
        "        end\n"
        "        __uranium_adv_original_add_experience(current,gain,growth)\n"
        "      end\n"
        "    end\n"
        "    class ::PokeBattle_Trainer\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_adv_original_money_earned\")\n"
        "        alias_method :__uranium_adv_original_money_earned, :moneyEarned\n"
        "      end\n"
        "      def moneyEarned\n"
        "        __uranium_adv_original_money_earned.to_i*$__uranium_trainer_money_multiplier.to_i\n"
        "      end\n"
        "    end\n"
        "    ObjectSpace.each_object(Class) do |klass|\n"
        "      begin\n"
        "        next unless klass==PokeBattle_Move || klass.ancestors.include?(PokeBattle_Move)\n"
        "        own=klass.instance_methods(false).collect { |m| m.to_s }\n"
        "        if own.include?(\"pbAccuracyCheck\") && !own.include?(\"__uranium_adv_original_accuracy\")\n"
        "          klass.class_eval do\n"
        "            alias_method :__uranium_adv_original_accuracy, :pbAccuracyCheck\n"
        "            define_method(:pbAccuracyCheck) do |attacker,opponent|\n"
        "              begin\n"
        "                if $__uranium_trainer_perfect_accuracy && attacker && opponent && @battle &&\n"
        "                   @battle.pbOwnedByPlayer?(attacker.index) && !@battle.pbOwnedByPlayer?(opponent.index)\n"
        "                  true\n"
        "                else\n"
        "                  __uranium_adv_original_accuracy(attacker,opponent)\n"
        "                end\n"
        "              rescue Exception\n"
        "                __uranium_adv_original_accuracy(attacker,opponent)\n"
        "              end\n"
        "            end\n"
        "          end\n"
        "        end\n"
        "        if own.include?(\"pbIsCritical?\") && !own.include?(\"__uranium_adv_original_critical\")\n"
        "          klass.class_eval do\n"
        "            alias_method :__uranium_adv_original_critical, :pbIsCritical?\n"
        "            define_method(:pbIsCritical?) do |attacker,opponent|\n"
        "              forced=false\n"
        "              begin\n"
        "                forced=$__uranium_trainer_guaranteed_critical && attacker && opponent && @battle &&\n"
        "                  @battle.pbOwnedByPlayer?(attacker.index) && !@battle.pbOwnedByPlayer?(opponent.index)\n"
        "              rescue Exception\n"
        "              end\n"
        "              forced ? true : __uranium_adv_original_critical(attacker,opponent)\n"
        "            end\n"
        "          end\n"
        "        end\n"
        "      rescue Exception\n"
        "      end\n"
        "    end\n"
        "    class ::PokeBattle_Battler\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_adv_original_status_set\")\n"
        "        alias_method :__uranium_adv_original_status_set, :status=\n"
        "      end\n"
        "      def status=(value)\n"
        "        begin\n"
        "          return @status if value.to_i>0 && $__uranium_trainer_status_immunity && @battle.pbOwnedByPlayer?(@index)\n"
        "        rescue Exception\n"
        "        end\n"
        "        __uranium_adv_original_status_set(value)\n"
        "      end\n"
        "    end\n"
        "    module ::UraniumTrainerAdvantageInstaller\n"
        "      def self.install_status_guard(klass,meth,original)\n"
        "        klass.class_eval do\n"
        "          alias_method original,meth\n"
        "          define_method(meth) do |*args|\n"
        "            immune=false\n"
        "            begin\n"
        "              immune=$__uranium_trainer_status_immunity && @battle.pbOwnedByPlayer?(@index)\n"
        "            rescue Exception\n"
        "            end\n"
        "            immune ? false : send(original,*args)\n"
        "          end\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    [:pbCanSleep?,:pbCanSleepYawn?,:pbCanPoison?,:pbCanPoisonSynchronize?,\n"
        "     :pbCanPoisonSpikes?,:pbCanBurn?,:pbCanBurnFromFireMove?,:pbCanBurnSynchronize?,\n"
        "     :pbCanParalyze?,:pbCanParalyzeSynchronize?,:pbCanFreeze?,:pbCanConfuse?,\n"
        "     :pbCanConfuseSelf?,:pbCanAttract?].each do |meth|\n"
        "      begin\n"
        "        klass=PokeBattle_Battler\n"
        "        names=klass.instance_methods.collect { |m| m.to_s }\n"
        "        original=(\"__uranium_adv_original_\"+meth.to_s.gsub(/[^A-Za-z0-9_]/,\"_\")).to_sym\n"
        "        next if !names.include?(meth.to_s) || names.include?(original.to_s)\n"
        "        UraniumTrainerAdvantageInstaller.install_status_guard(klass,meth,original)\n"
        "      rescue Exception\n"
        "      end\n"
        "    end\n"
        "    class ::PokeBattle_Pokemon\n"
        "      unless instance_methods.collect { |m| m.to_s }.include?(\"__uranium_adv_original_pokemon_status_set\")\n"
        "        alias_method :__uranium_adv_original_pokemon_status_set, :status=\n"
        "      end\n"
        "      def status=(value)\n"
        "        begin\n"
        "          owned=$Trainer && $Trainer.party && $Trainer.party.any? { |p| p.equal?(self) }\n"
        "          return @status if value.to_i>0 && $__uranium_trainer_status_immunity && owned\n"
        "        rescue Exception\n"
        "        end\n"
        "        __uranium_adv_original_pokemon_status_set(value)\n"
        "      end\n"
        "    end\n"
        "    class Object\n"
        "      all_methods=instance_methods.collect { |m| m.to_s }+private_instance_methods.collect { |m| m.to_s }\n"
        "      unless all_methods.include?(\"__uranium_adv_original_fishing\")\n"
        "        alias_method :__uranium_adv_original_fishing, :pbFishing\n"
        "        alias_method :__uranium_adv_original_wait_fishing, :pbWaitMessage\n"
        "        alias_method :__uranium_adv_original_fishing_input, :pbWaitForInput\n"
        "      end\n"
        "      def pbFishing(hasencounter,rodtype=1)\n"
        "        active=$__uranium_trainer_instant_fishing || $__uranium_trainer_guaranteed_fishing\n"
        "        $__uranium_trainer_fishing_scope+=1 if active\n"
        "        begin\n"
        "          __uranium_adv_original_fishing(hasencounter,rodtype)\n"
        "        ensure\n"
        "          $__uranium_trainer_fishing_scope-=1 if active\n"
        "        end\n"
        "      end\n"
        "      def pbWaitMessage(msgwindow,time)\n"
        "        return false if $__uranium_trainer_fishing_scope.to_i>0 && $__uranium_trainer_instant_fishing\n"
        "        __uranium_adv_original_wait_fishing(msgwindow,time)\n"
        "      end\n"
        "      def pbWaitForInput(msgwindow,message,frames)\n"
        "        if $__uranium_trainer_fishing_scope.to_i>0 &&\n"
        "           ($__uranium_trainer_instant_fishing || $__uranium_trainer_guaranteed_fishing)\n"
        "          return true\n"
        "        end\n"
        "        __uranium_adv_original_fishing_input(msgwindow,message,frames)\n"
        "      end\n"
        "      private :pbFishing, :pbWaitMessage, :pbWaitForInput\n"
        "    end\n"
        "    module ::Kernel\n"
        "      unless private_instance_methods.collect { |m| m.to_s }.include?(\"__uranium_adv_original_fishing_rand\")\n"
        "        alias_method :__uranium_adv_original_fishing_rand, :rand\n"
        "      end\n"
        "      def rand(limit=0)\n"
        "        if $__uranium_trainer_fishing_scope.to_i>0 &&\n"
        "           $__uranium_trainer_guaranteed_fishing && limit.to_i==100\n"
        "          return 0\n"
        "        end\n"
        "        __uranium_adv_original_fishing_rand(limit)\n"
        "      end\n"
        "      private :rand\n"
        "    end\n"
        "    if defined?(::ItemHandlers) && defined?(::EncounterTypes)\n"
        "      $__uranium_trainer_main_object||=self\n"
        "      if !$__uranium_trainer_original_rod_handlers\n"
        "        $__uranium_trainer_original_rod_handlers={}\n"
        "        [:OLDROD,:GOODROD,:SUPERROD].each do |rod|\n"
        "          $__uranium_trainer_original_rod_handlers[rod]=ItemHandlers::UseInField[rod]\n"
        "        end\n"
        "      end\n"
        "      module ::UraniumTrainerFishing\n"
        "        def self.use(item,rod,enctype,rodtype)\n"
        "          guaranteed=$__uranium_trainer_guaranteed_fishing ? true : false\n"
        "          if !guaranteed\n"
        "            original=$__uranium_trainer_original_rod_handlers[rod]\n"
        "            return original.call(item) if original\n"
        "            return false\n"
        "          end\n"
        "          terrain=Kernel.pbFacingTerrainTag\n"
        "          not_cliff=$game_map.passable?($game_player.x,$game_player.y,$game_player.direction)\n"
        "          if !PBTerrain.isWater?(terrain) || (!not_cliff && !$PokemonGlobal.surfing)\n"
        "            Kernel.pbMessage(_INTL(\"Can't use that here.\"))\n"
        "            return false\n"
        "          end\n"
        "          chosen=enctype\n"
        "          has_encounter=$PokemonEncounters.hasEncounter?(chosen)\n"
        "          if guaranteed && !has_encounter\n"
        "            [EncounterTypes::OldRod,EncounterTypes::GoodRod,EncounterTypes::SuperRod].each do |candidate|\n"
        "              if $PokemonEncounters.hasEncounter?(candidate)\n"
        "                chosen=candidate; has_encounter=true; break\n"
        "              end\n"
        "            end\n"
        "          end\n"
        "          success=guaranteed && has_encounter\n"
        "          if !guaranteed && has_encounter\n"
        "            bitechance=20+(25*rodtype)\n"
        "            if $Trainer.party.length>0 && !$Trainer.party[0].isEgg?\n"
        "              bitechance*=2 if isConst?($Trainer.party[0].ability,PBAbilities,:STICKYHOLD)\n"
        "              bitechance*=2 if isConst?($Trainer.party[0].ability,PBAbilities,:SUCTIONCUPS)\n"
        "            end\n"
        "            success=rand(100)<bitechance\n"
        "          end\n"
        "          if !success\n"
        "            Kernel.pbMessage(_INTL(\"Not even a nibble...\"))\n"
        "            return false\n"
        "          end\n"
        "          $__uranium_trainer_main_object.send(:pbEncounter,chosen)\n"
        "          return true\n"
        "        end\n"
        "      end\n"
        "      ItemHandlers::UseInField.add(:OLDROD,proc { |item| UraniumTrainerFishing.use(item,:OLDROD,EncounterTypes::OldRod,1) })\n"
        "      ItemHandlers::UseInField.add(:GOODROD,proc { |item| UraniumTrainerFishing.use(item,:GOODROD,EncounterTypes::GoodRod,2) })\n"
        "      ItemHandlers::UseInField.add(:SUPERROD,proc { |item| UraniumTrainerFishing.use(item,:SUPERROD,EncounterTypes::SuperRod,3) })\n"
        "    end\n"
        "    if $__uranium_trainer_status_immunity && $Trainer && $Trainer.party\n"
        "      $Trainer.party.compact.each do |p|\n"
        "        p.status=0; p.statusCount=0 if p.respond_to?(:statusCount=)\n"
        "      end\n"
        "      if $scene && $scene.respond_to?(:battle) && $scene.battle\n"
        "        $scene.battle.battlers.each do |b|\n"
        "          next if !b || !$scene.battle.pbOwnedByPlayer?(b.index)\n"
        "          b.status=0; b.statusCount=0 if b.respond_to?(:statusCount=)\n"
        "        end\n"
        "      end\n"
        "    end\n"
        "    installed=1\n"
        "  end\n"
        "rescue Exception\n"
        "end\n"
        "begin\n"
        "  Win32API.new(\"kernel32\",\"RtlMoveMemory\",[\"l\",\"p\",\"l\"],\"v\").call(%lu,[installed].pack(\"l\"),4)\n"
        "rescue Exception\n"
        "end\n",
        InterlockedExchangeAdd(&s_always_first, 0) ? "true" : "false",
        InterlockedExchangeAdd(&s_accuracy, 0) ? "true" : "false",
        InterlockedExchangeAdd(&s_critical, 0) ? "true" : "false",
        InterlockedExchangeAdd(&s_status_immunity, 0) ? "true" : "false",
        InterlockedExchangeAdd(&s_flee, 0) ? "true" : "false",
        InterlockedExchangeAdd(&s_guaranteed_fishing, 0) ? "true" : "false",
        clamp_multiplier((int)InterlockedExchangeAdd(&s_exp_multiplier, 0)),
        clamp_multiplier((int)InterlockedExchangeAdd(&s_money_multiplier, 0)),
        (unsigned long)(ULONG_PTR)&s_installed);
    s_ruby[sizeof(s_ruby) - 1] = '\0';
}

static void __cdecl on_game_thread_tick(void*) {
    if (InterlockedExchange(&s_pending, 0) == 0) return;
    build_ruby();
    if (rgss_safe_eval(s_ruby) != 0) InterlockedExchange(&s_pending, 1);
}

static DWORD WINAPI retry_thread(LPVOID) {
    while (InterlockedExchangeAdd(&s_installed, 0) == 0 &&
           !rgss_safe_dispatch_is_stopping()) {
        InterlockedExchange(&s_pending, 1);
        notify_game();
        Sleep(500);
    }
    InterlockedExchange(&s_retry_started, 0);
    return 0;
}

static void ensure_retry_thread() {
    if (InterlockedExchangeAdd(&s_installed, 0) != 0 ||
        InterlockedCompareExchange(&s_retry_started, 1, 0) != 0) return;
    HANDLE thread = CreateThread(NULL, 0, retry_thread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
    else InterlockedExchange(&s_retry_started, 0);
}

static void publish() {
    InterlockedExchange(&s_pending, 1);
    notify_game();
    ensure_retry_thread();
}

static void save_bool(const char* key, bool value) {
    WritePrivateProfileStringA("Settings", key, value ? "1" : "0", s_ini);
}

static void save_int(const char* key, int value) {
    char text[16] = {};
    wsprintfA(text, "%d", value);
    WritePrivateProfileStringA("Settings", key, text, s_ini);
}

void opt_advantages_init(const char* ini_path) {
    lstrcpynA(s_ini, ini_path ? ini_path : "", MAX_PATH);
#define LOAD_BOOL(field, atomic, key) do { \
    field=GetPrivateProfileIntA("Settings", key, 0, s_ini)!=0; \
    InterlockedExchange(&atomic, field ? 1 : 0); \
} while (0)
    LOAD_BOOL(g_always_first_enabled, s_always_first, "AlwaysFirstEnabled");
    LOAD_BOOL(g_perfect_accuracy_enabled, s_accuracy, "PerfectAccuracyEnabled");
    LOAD_BOOL(g_guaranteed_critical_enabled, s_critical, "GuaranteedCriticalEnabled");
    LOAD_BOOL(g_status_immunity_enabled, s_status_immunity, "StatusImmunityEnabled");
    LOAD_BOOL(g_guaranteed_flee_enabled, s_flee, "GuaranteedFleeEnabled");
    // Option historique supprimee : elle ne doit plus influencer le runtime.
    g_instant_fishing_enabled=false;
    InterlockedExchange(&s_instant_fishing, 0);
    WritePrivateProfileStringA("Settings", "InstantFishingEnabled", NULL, s_ini);
    LOAD_BOOL(g_guaranteed_fishing_enabled, s_guaranteed_fishing, "GuaranteedFishingEnabled");
#undef LOAD_BOOL
    g_exp_multiplier=clamp_multiplier(GetPrivateProfileIntA("Settings", "ExpMultiplier", 1, s_ini));
    g_prize_money_multiplier=clamp_multiplier(GetPrivateProfileIntA("Settings", "PrizeMoneyMultiplier", 1, s_ini));
    InterlockedExchange(&s_exp_multiplier, g_exp_multiplier);
    InterlockedExchange(&s_money_multiplier, g_prize_money_multiplier);
    InterlockedExchange(&s_pending, 1);
    InterlockedExchange(&s_installed, 0);
}

void opt_advantages_set_hwnd_and_start(HWND) {
    rgss_safe_dispatch_register(on_game_thread_tick, NULL);
    publish();
}

#define DEFINE_BOOL_SETTER(name, field, atomic, key) \
void name(bool enabled) { \
    field=enabled; InterlockedExchange(&atomic, enabled ? 1 : 0); \
    save_bool(key, enabled); publish(); \
}
DEFINE_BOOL_SETTER(opt_advantages_set_always_first, g_always_first_enabled, s_always_first, "AlwaysFirstEnabled")
DEFINE_BOOL_SETTER(opt_advantages_set_perfect_accuracy, g_perfect_accuracy_enabled, s_accuracy, "PerfectAccuracyEnabled")
DEFINE_BOOL_SETTER(opt_advantages_set_guaranteed_critical, g_guaranteed_critical_enabled, s_critical, "GuaranteedCriticalEnabled")
DEFINE_BOOL_SETTER(opt_advantages_set_status_immunity, g_status_immunity_enabled, s_status_immunity, "StatusImmunityEnabled")
DEFINE_BOOL_SETTER(opt_advantages_set_guaranteed_flee, g_guaranteed_flee_enabled, s_flee, "GuaranteedFleeEnabled")
DEFINE_BOOL_SETTER(opt_advantages_set_guaranteed_fishing, g_guaranteed_fishing_enabled, s_guaranteed_fishing, "GuaranteedFishingEnabled")
#undef DEFINE_BOOL_SETTER

// Conserve l'entree interne pour la remise a zero des anciennes versions,
// mais l'ancien cheat n'est plus expose ni injecte dans le jeu.
void opt_advantages_set_instant_fishing(bool) {
    g_instant_fishing_enabled=false;
    InterlockedExchange(&s_instant_fishing, 0);
    WritePrivateProfileStringA("Settings", "InstantFishingEnabled", NULL, s_ini);
    publish();
}

void opt_advantages_set_exp_multiplier(int multiplier) {
    g_exp_multiplier=clamp_multiplier(multiplier);
    InterlockedExchange(&s_exp_multiplier, g_exp_multiplier);
    save_int("ExpMultiplier", g_exp_multiplier);
    publish();
}

void opt_advantages_set_money_multiplier(int multiplier) {
    g_prize_money_multiplier=clamp_multiplier(multiplier);
    InterlockedExchange(&s_money_multiplier, g_prize_money_multiplier);
    save_int("PrizeMoneyMultiplier", g_prize_money_multiplier);
    publish();
}
