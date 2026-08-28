#pragma once
#include <Arduino.h>
#include "content.h"
#include "nature.h"

using Lang = uint8_t;
constexpr Lang LANG_RECOVERY = 0xFF;

extern Lang gLang;  // idioma activo (definido en i18n.cpp)

// IDs de cadena. El orden debe coincidir con la tabla STRINGS de i18n.cpp.
enum StrId : uint8_t {
  // estado del bicho (statusMsg)
  S_EVOLVING, S_EATING, S_LIKES, S_HUNGRY, S_NEEDS_BATH,
  S_EXHAUSTED, S_SAD, S_CHUBBY, S_IS_SHINY, S_HAPPY,
  // ceremonias de despedida
  S_FAREWELL, S_RUNAWAY, S_GOODBYE,
  // huevo
  S_EGG_HDR, S_EGG_LEGEND, S_EGG_RARE, S_EGG_TOUCH, S_EGG_MOVES, S_EGG_ALMOST,
  // formatos compartidos
  S_POKEDEX_FMT,   // "POKEDEX %u/151"
  S_NAME_FMT,      // "%s%s Nv.%u"
  // dialogo soltar
  S_RELEASE_FMT, S_YES, S_NO,
  // minijuego y saco
  S_HITS_FMT, S_STR_GAIN_FMT, S_NEW_RECORD, S_RECORD_FMT, S_HIT_FAST,
  S_SCORE_FMT, S_GREAT_JOY, S_PLUS_JOY,
  // reloj / ajustes
  S_SET_TIME, S_HOUR, S_MIN, S_CLOCK_CANCEL, S_LANG_LABEL,
  // celebracion
  S_MEDAL_BANNER, S_GREAT, S_STREAK_DAYS_FMT,
  // ficha: perfil
  S_STREAK_FMT, S_VIN, S_BERRY_UNK, S_BERRY_RED, S_BERRY_BLUE, S_BERRY_GREEN,
  S_INFO_FMT, S_RENAME_HINT,
  // ficha: combate
  S_BATTLE, S_STAT_ATK, S_STAT_DEF, S_STAT_SPE, S_STAT_WGT, S_TRAIN_STR,
  // ficha: medallas
  S_MEDALS_FMT, S_BACK,
  // teclado y galeria
  S_NAME, S_DETAIL_BACK,
  // barras
  S_BAR_FOOD, S_BAR_JOY, S_BAR_ENE, S_BAR_HYG,
  // marcador en vivo del minijuego
  S_REC_FMT,
  // ficha: pagina de progreso
  S_PROGRESS, S_LVL_FMT, S_NEXT_LVL_FMT, S_EVO_LABEL, S_FINAL_FORM,
  S_EVO_READY, S_EVO_BLOCKED, S_EVO_IN_FMT, S_MISTAKES_FMT,
  // interruptor de sonido (ajustes)
  S_SND_ON, S_SND_OFF,
  S_EVO_TAP,        // texto del boton de evolucion
  S_FAREWELL_BTN,   // texto del boton de despedida (lleva el nombre: "%s ...")
  S_RUNAWAY_BTN,    // texto del boton de escapada por abandono (final triste)
  // dialogos de decision (evolucionar/mantener, despedirse/quedaros)
  S_EVO_Q, S_EVO_KEEP, S_FAR_Q, S_FAR_GO, S_FAR_STAY,
  S_CHOOSE_STARTER,  // titulo de la eleccion del inicial (primera vez)
  S_NO_SPRITES, S_LOAD_SPRITES,  // aviso cuando falta el sprite en la SD
  S_STAT_VIT,   // vitalidad (abreviatura de PS/HP en cada idioma)
  S_IV_FMT,     // valor individual junto a cada stat ("IV %u")
  // menu overlay (tap the name on the main screen) and the party
  S_MENU_TITLE, S_SETTINGS, S_CLOSE,
  S_PARTY_FMT,      // "PARTY %u/6"
  S_PARTY_EMPTY,    // empty slot marker
  S_PARTY_JOINED,   // "%s joined the party!"
  S_PARTY_FULL,     // prompt: pick someone to replace
  S_PARTY_LETGO,    // decline the swap
  // stats shortcut in the menu, and the training submenu behind the 5th icon
  S_STATS,          // menu row: jump straight to the stats card page
  S_TRAIN,          // training submenu title
  S_TR_ATK,         // row: train strength (the punching bag)
  S_TR_SPE,         // row: train speed (the ball game)
  S_TR_DEF,         // row: defence -- passive, shown for information only
  S_TR_DEF_HINT,    // why the defence row does nothing when tapped
  // moves card page + the move picker behind it
  S_MOVES,          // card page title
  S_MOVE_EMPTY,     // an unused move slot
  S_MOVE_PICK,      // picker title: choose what goes in this slot
  S_MOVE_TAP,       // hint on the moves page: tap a slot to change it
  S_MOVE_PWR,       // "PWR %u" next to an attacking move
  S_MOVE_STATUS,    // shown instead of power for a status move
  // level-up learn prompt
  S_LEARN_Q,        // "%s wants to learn" (name of the creature)
  S_LEARN_SKIP,     // decline the new move
  // battle
  S_BTL_USED,       // "%s used %s"
  S_BTL_SUPER, S_BTL_WEAK, S_BTL_IMMUNE,
  S_BTL_MISS, S_BTL_CRIT, S_BTL_FAINT,
  S_BTL_HURTSELF,   // confusion
  S_BTL_STATUS,     // "%s was %s" -- ailment name comes from S_AIL_*
  S_AIL_PARA, S_AIL_BURN, S_AIL_POISON, S_AIL_SLEEP, S_AIL_FREEZE, S_AIL_CONFUSE,
  S_BTL_WIN, S_BTL_LOSE,
  S_BTL_SENDS,      // "%s sends out %s"
  S_BTL_GO,         // "Go, %s!"
  S_GYMS,           // gym list title
  S_BADGES_FMT,     // "BADGES %u/8"
  S_TRAINER,        // player card title
  S_SPD_GAIN_FMT,   // "SPEED +%u" after the reaction test
  S_AVATAR_HINT,    // "tap: change avatar"
  S_MEDALS_TOTAL_FMT,  // "%u earned in all"
  S_EASY, S_HARD,   // gym ladder difficulty toggle
  S_PICK_FMT,       // "CHOSEN %u/%u"
  S_FIGHT,          // start the battle
  S_LOCKED,         // a gym not yet unlocked
  S_BTL_SWITCH,     // battle menu: bring on another creature
  S_BTL_BEAT,       // "%s defeated!" on the win screen
  S_BTL_NEWBADGE,   // a badge earned for the first time
  S_VOL_FMT,        // volume level, e.g. "VOL 7"
  S_BOX_FMT,        // "BOX %u/%u"
  S_BOX_SWAP,       // "swap with %s: pick a slot"
  S_BOX_DEPOSIT,    // "deposit in empty slot: pick a member"
  S_BOX_BTN,        // compact Box label
  S_REVIVE,         // bring a banked creature back as a companion
  S_REVIVE_EGG,     // why it is unavailable right now
  // LAN battle
  S_LAN, S_LAN_HOST, S_LAN_JOIN, S_LAN_WAIT, S_LAN_READY, S_LAN_REFUSED,
  S_LAN_PICK, S_LAN_VS,
  S_LAN_GONE, S_LAN_WAITFOE, S_LAN_REMATCH,
  S_BTL_RUN,        // the third option in a fight: leave it
  S_EGG_REGION,     // hint under the egg's region pill
  S_WIN_TRAIN_FMT,  // "%s IV +%u" -- the innate reward from this gym
  S_WIN_MAXED,      // ... or that all four IVs were already maxed
  S_CHOOSE_REGION,  // first boot: pick the region your starter comes from
  S_RETIRE,         // menu row: release before farewell is earned
  S_RETIRE_Q,       // ...and the confirmation
  S_RETIRE_COST,    // what retiring early costs the next creature
  S_EVO_SLOW,       // shown on the card while a creature carries that debt
  S_NEED_PACK,      // region chooser: this region's sprite pack is not on the SD
  S_MOVE_CHANGE,    // explicit action on the move description screen
  // care quiz modal
  S_QUIZ_TITLE, S_QUIZ_ANSWER, S_QUIZ_CORRECT, S_QUIZ_WRONG,
  S_QUIZ_TIMEOUT, S_QUIZ_EFFECT_FMT,
  S_NATURE_FMT,     // "NATURE: %s" on live and banked creature sheets
  // Keep these 25 contiguous and in NatureId order; natureName() indexes them.
  S_NATURE_HARDY, S_NATURE_LONELY, S_NATURE_BRAVE, S_NATURE_ADAMANT,
  S_NATURE_NAUGHTY, S_NATURE_BOLD, S_NATURE_DOCILE, S_NATURE_RELAXED,
  S_NATURE_IMPISH, S_NATURE_LAX, S_NATURE_TIMID, S_NATURE_HASTY,
  S_NATURE_SERIOUS, S_NATURE_JOLLY, S_NATURE_NAIVE, S_NATURE_MODEST,
  S_NATURE_MILD, S_NATURE_QUIET, S_NATURE_BASHFUL, S_NATURE_RASH,
  S_NATURE_CALM, S_NATURE_GENTLE, S_NATURE_SASSY, S_NATURE_CAREFUL,
  S_NATURE_QUIRKY,
  // bag and battle-centre navigation (append-only: UI packs are positional)
  S_BAG, S_BATTLE_CENTER, S_USE, S_ITEM_EMPTY, S_ITEM_CANT_USE,
  S_WILD, S_CAUGHT, S_JOIN, S_ITEM_FOUND_FMT,
  // persistent battle death and burial (append-only)
  S_DEAD, S_BURY, S_BURY_Q, S_BTL_RUN_FAILED, S_BTL_FOE_RAN,
  // battle effect feedback (append-only)
  S_STAT_SPA, S_STAT_SPD,
  S_BTL_STAGE_FMT,  // combatant, stat label, signed stage delta
  S_BTL_HEALED,     // combatant recovered HP
  S_BOX_WITHDRAW,   // move a Box member into the first free cultivation slot
  // weather and terrain feedback (append-only)
  S_FIELD_SUN, S_FIELD_RAIN, S_FIELD_SAND, S_FIELD_SNOW,
  S_FIELD_ELECTRIC, S_FIELD_GRASSY, S_FIELD_MISTY, S_FIELD_PSYCHIC,
  S_BTL_FIELD_BEGAN, S_BTL_FIELD_ENDED, S_BTL_FIELD_BLOCKED,
  S_BTL_SAND_HURT,
  S_REWARDS,        // battle victory settlement title
  S_WIN_TRAINING_FMT, // "%s TRAINING +%u" -- decayable wild-battle reward
  // menu power-off confirmation (append-only)
  S_POWER_OFF, S_POWER_OFF_Q,
  // nature details modal (append-only)
  S_NATURE_EFFECT, S_NATURE_STAT_EFFECT_FMT, S_NATURE_TRAIN_EFFECT_FMT,
  S_STAT_ACC, S_STAT_EVA,
  S_BTL_FORM,
  STR_COUNT
};

const char *T(StrId id);       // texto en el idioma activo
const char *medalName(int i);  // banner de medalla (MED_COUNT)
const char *medalLabel(int i); // etiqueta corta de medalla
const char *medalDesc(int i);  // descripcion larga de medalla
const char *natureName(NatureId nature);
const char *natureDescription(NatureId nature);

bool loadLang();             // carga NVS; false si aun debe elegirse idioma
bool setLang(Lang l);        // cambia y persiste el idioma
uint8_t langCount();
const char *langCode(Lang l);
const char *langLabel(Lang l);
const char *langDisplayName(Lang l);
bool langIsCjk(Lang l);
