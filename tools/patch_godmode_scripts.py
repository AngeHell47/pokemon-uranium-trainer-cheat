#!/usr/bin/env python3
"""Patch a decrypted Pokemon Uranium Scripts.rxdata for total HP lock."""

import argparse
import os
import sys
import zlib

try:
    from rubymarshal.reader import loads
    from rubymarshal.writer import writes
except ImportError:
    sys.stderr.write(
        "Missing dependency: install tools/requirements-godmode.txt first.\n"
    )
    raise


def replace_once(source, old, new, label):
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"{label}: expected exactly one match in this game revision, "
            f"found {count}"
        )
    return source.replace(old, new, 1)


def ruby_path(path):
    return os.path.abspath(path).replace("\\", "/").replace("'", "\\'")


def patch_scripts(source_path, output_path, trainer_ini_path):
    with open(source_path, "rb") as handle:
        scripts = loads(handle.read())
    if len(scripts) <= 122:
        raise RuntimeError("Unsupported Scripts.rxdata: expected at least 123 scripts")

    ini = ruby_path(trainer_ini_path).encode("utf-8")

    battler = zlib.decompress(scripts[83][2])
    lock_initialization = (
        b"begin\n"
        b"  trainer_settings=File.open('" + ini + b"','rb') { |f| f.read }\n"
        b"  $__uranium_trainer_hp_lock=(trainer_settings =~ "
        b"/^\\s*HpLock\\s*=\\s*1\\s*$/i) ? true : false\n"
        b"rescue Exception\n"
        b"  $__uranium_trainer_hp_lock=false if "
        b"$__uranium_trainer_hp_lock.nil?\n"
        b"end\n"
    )
    battler = lock_initialization + battler
    battler = replace_once(
        battler,
        b"  def hp=(value)\n"
        b"    @hp=value.to_i\n"
        b"    @pokemon.hp=value.to_i if @pokemon\n"
        b"  end\n",
        b"  def hp=(value)\n"
        b"    trainer_owned=false\n"
        b"    begin\n"
        b"      trainer_owned=$__uranium_trainer_hp_lock && @battle && "
        b"@pokemon && @battle.pbOwnedByPlayer?(@index)\n"
        b"    rescue Exception\n"
        b"      trainer_owned=false\n"
        b"    end\n"
        b"    return @hp if trainer_owned && @hp && value.to_i<@hp.to_i\n"
        b"    @hp=value.to_i\n"
        b"    @pokemon.hp=value.to_i if @pokemon\n"
        b"  end\n",
        "PokeBattle_Battler#hp=",
    )
    battler = replace_once(
        battler,
        b"  def pbReduceHP(amt,anim=false)\n"
        b"    if amt>=self.hp\n",
        b"  def pbReduceHP(amt,anim=false)\n"
        b"    begin\n"
        b"      return 0 if $__uranium_trainer_hp_lock && @battle && "
        b"@pokemon && @battle.pbOwnedByPlayer?(@index)\n"
        b"    rescue Exception\n"
        b"    end\n"
        b"    if amt>=self.hp\n",
        "PokeBattle_Battler#pbReduceHP",
    )
    scripts[83][2] = zlib.compress(battler, 9)

    move = zlib.decompress(scripts[85][2])
    move = replace_once(
        move,
        b"  def pbReduceHPDamage(damage,attacker,opponent)\n"
        b"    endure=false\n",
        b"  def pbReduceHPDamage(damage,attacker,opponent)\n"
        b"    begin\n"
        b"      if $__uranium_trainer_hp_lock && opponent && @battle && "
        b"@battle.pbOwnedByPlayer?(opponent.index)\n"
        b"        opponent.damagestate.calcdamage=0\n"
        b"        opponent.damagestate.hplost=0\n"
        b"        opponent.damagestate.substitute=false\n"
        b"        return 0\n"
        b"      end\n"
        b"    rescue Exception\n"
        b"    end\n"
        b"    endure=false\n",
        "PokeBattle_Move#pbReduceHPDamage",
    )
    scripts[85][2] = zlib.compress(move, 9)

    pokemon = zlib.decompress(scripts[122][2])
    pokemon = replace_once(
        pokemon,
        b"  def hp=(value)\r\n"
        b"    value=0 if value<0\r\n"
        b"    @hp=value\r\n"
        b"    if @hp==0\r\n"
        b"      @status=0\r\n"
        b"      @statusCount=0\r\n"
        b"    end\r\n"
        b"  end\r\n",
        b"  def hp=(value)\r\n"
        b"    trainer_owned=false\r\n"
        b"    begin\r\n"
        b"      if $__uranium_trainer_hp_lock && $Trainer && $Trainer.party\r\n"
        b"        for trainer_pokemon in $Trainer.party\r\n"
        b"          if trainer_pokemon.equal?(self)\r\n"
        b"            trainer_owned=true\r\n"
        b"            break\r\n"
        b"          end\r\n"
        b"        end\r\n"
        b"      end\r\n"
        b"    rescue Exception\r\n"
        b"      trainer_owned=false\r\n"
        b"    end\r\n"
        b"    return @hp if trainer_owned && @hp && value.to_i<@hp.to_i\r\n"
        b"    value=0 if value<0\r\n"
        b"    @hp=value\r\n"
        b"    if @hp==0\r\n"
        b"      @status=0\r\n"
        b"      @statusCount=0\r\n"
        b"    end\r\n"
        b"  end\r\n",
        "PokeBattle_Pokemon#hp=",
    )
    scripts[122][2] = zlib.compress(pokemon, 9)

    payload = writes(scripts)
    check = loads(payload)
    check_battler = zlib.decompress(check[83][2])
    check_move = zlib.decompress(check[85][2])
    check_pokemon = zlib.decompress(check[122][2])
    assert b"trainer_settings=File.open" in check_battler
    assert b"trainer_owned=$__uranium_trainer_hp_lock" in check_battler
    assert b"@battle.pbOwnedByPlayer?(opponent.index)" in check_move
    assert b"trainer_pokemon.equal?(self)" in check_pokemon

    output_dir = os.path.dirname(os.path.abspath(output_path))
    os.makedirs(output_dir, exist_ok=True)
    temporary = output_path + ".tmp"
    with open(temporary, "wb") as handle:
        handle.write(payload)
    os.replace(temporary, output_path)
    return len(payload), len(check)


def main():
    parser = argparse.ArgumentParser(
        description=(
            "Patch a decrypted Pokemon Uranium 1.2/1.3 Scripts.rxdata so "
            "the trainer's HP lock cancels every player HP decrease."
        )
    )
    parser.add_argument("source", help="original decrypted Scripts.rxdata")
    parser.add_argument("output", help="destination Data/Scripts.rxdata")
    parser.add_argument(
        "--trainer-ini",
        help=(
            "trainer.ini read when Ruby starts; defaults to the parent of "
            "the output Data directory"
        ),
    )
    args = parser.parse_args()

    output = os.path.abspath(args.output)
    trainer_ini = args.trainer_ini
    if not trainer_ini:
        trainer_ini = os.path.join(os.path.dirname(os.path.dirname(output)),
                                   "trainer.ini")

    size, count = patch_scripts(
        os.path.abspath(args.source), output, os.path.abspath(trainer_ini)
    )
    print(f"Wrote {output} ({size} bytes, {count} scripts)")


if __name__ == "__main__":
    main()
