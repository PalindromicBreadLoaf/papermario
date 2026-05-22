
#include "common.h"
#include "battle/battle.h"
#include "ld_addrs.h"

extern ActorBlueprint battle_partner_goombario;
extern ActorBlueprint battle_partner_kooper;
extern ActorBlueprint battle_partner_bombette;
extern ActorBlueprint battle_partner_parakarry;
extern ActorBlueprint battle_partner_goompa;
extern ActorBlueprint battle_partner_watt;
extern ActorBlueprint battle_partner_sushie;
extern ActorBlueprint battle_partner_lakilester;
extern ActorBlueprint battle_partner_bow;
extern ActorBlueprint battle_partner_twink;

#ifdef BUILD_PC
#define BATTLE_PARTNER_ROM_START_goombario 0x006f10e0u
#define BATTLE_PARTNER_ROM_END_goombario 0x006f5e80u
#define BATTLE_PARTNER_ROM_START_kooper 0x006f5e80u
#define BATTLE_PARTNER_ROM_END_kooper 0x006fad10u
#define BATTLE_PARTNER_ROM_START_bombette 0x006fad10u
#define BATTLE_PARTNER_ROM_END_bombette 0x006ffd80u
#define BATTLE_PARTNER_ROM_START_parakarry 0x006ffd80u
#define BATTLE_PARTNER_ROM_END_parakarry 0x00703af0u
#define BATTLE_PARTNER_ROM_START_goompa 0x006f0b30u
#define BATTLE_PARTNER_ROM_END_goompa 0x006f10e0u
#define BATTLE_PARTNER_ROM_START_watt 0x00703af0u
#define BATTLE_PARTNER_ROM_END_watt 0x00707ca0u
#define BATTLE_PARTNER_ROM_START_sushie 0x00707ca0u
#define BATTLE_PARTNER_ROM_END_sushie 0x0070bd10u
#define BATTLE_PARTNER_ROM_START_lakilester 0x0070bd10u
#define BATTLE_PARTNER_ROM_END_lakilester 0x00710ef0u
#define BATTLE_PARTNER_ROM_START_bow 0x00710ef0u
#define BATTLE_PARTNER_ROM_END_bow 0x00714cf0u
#define BATTLE_PARTNER_ROM_START_twink 0x00714cf0u
#define BATTLE_PARTNER_ROM_END_twink 0x00715850u

#define BATTLE_PARTNER_ROM_START(name) BATTLE_PARTNER_ROM_START_##name
#define BATTLE_PARTNER_ROM_END(name) BATTLE_PARTNER_ROM_END_##name
#else
#define BATTLE_PARTNER_ROM_START(name) (u32)battle_partner_##name##_ROM_START
#define BATTLE_PARTNER_ROM_END(name) (u32)battle_partner_##name##_ROM_END
#endif

#define BATTLE_PARTNER_ENTRY(name, Y) \
    { \
        BATTLE_PARTNER_ROM_START(name), \
        BATTLE_PARTNER_ROM_END(name), \
        battle_partner_##name##_VRAM, \
        &battle_partner_##name, \
        Y \
    }

PartnerDMAData bPartnerDmaTable[] = {
    {},
    BATTLE_PARTNER_ENTRY(goombario, 0),
    BATTLE_PARTNER_ENTRY(kooper, 0),
    BATTLE_PARTNER_ENTRY(bombette, 0),
    BATTLE_PARTNER_ENTRY(parakarry, 30),
    BATTLE_PARTNER_ENTRY(goompa, 0),
    BATTLE_PARTNER_ENTRY(watt, 20),
    BATTLE_PARTNER_ENTRY(sushie, 0),
    BATTLE_PARTNER_ENTRY(lakilester, 10),
    BATTLE_PARTNER_ENTRY(bow, 20),
    {},
    BATTLE_PARTNER_ENTRY(twink, 30),
};
